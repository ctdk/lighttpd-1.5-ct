#include <sys/types.h>

#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

#include "fdevent.h"
#include "buffer.h"
#include "log.h"

#include "sys-socket.h"

fdevent_revent *fdevent_revent_init(void) {
	STRUCT_INIT(fdevent_revent, revent);

	return revent;
}

void fdevent_revent_free(fdevent_revent *revent) {
	if (!revent) return;

	free(revent);
}

fdevent_revents *fdevent_revents_init(void) {
	STRUCT_INIT(fdevent_revents, revents);

	return revents;
}

void fdevent_revents_reset(fdevent_revents *revents) {
	if (!revents) return;

	revents->used = 0;
}

void fdevent_revents_add(fdevent_revents *revents, int fd, int events) {
	fdevent_revent *revent;

	if (revents->used == revents->size) {
		/* resize the events-array */
		revents->ptr = realloc(revents->ptr, (revents->size + 1) * sizeof(*(revents->ptr)));
		revents->ptr[revents->size++] = fdevent_revent_init();
	}

	revent = revents->ptr[revents->used++];
	revent->fd = fd;
	revent->revents = events;
}

void fdevent_revents_free(fdevent_revents *revents) {
	size_t i;
	
	if (!revents) return;

	if (revents->size) {
		for (i = 0; i < revents->size; i++) {
			fdevent_revent_free(revents->ptr[i]);
		}

		free(revents->ptr);
	}
	free(revents);
}

fdevents *fdevent_init(size_t maxfds, fdevent_handler_t type) {
	fdevents *ev;

	ev = calloc(1, sizeof(*ev));
	ev->fdarray = calloc(maxfds, sizeof(*ev->fdarray));
	ev->maxfds = maxfds;

	switch(type) {
	case FDEVENT_HANDLER_POLL:
		if (0 != fdevent_poll_init(ev)) {
			fprintf(stderr, "%s.%d: event-handler poll failed\n",
				__FILE__, __LINE__);

			return NULL;
		}
		break;
	case FDEVENT_HANDLER_SELECT:
		if (0 != fdevent_select_init(ev)) {
			fprintf(stderr, "%s.%d: event-handler select failed\n",
				__FILE__, __LINE__);
			return NULL;
		}
		break;
	case FDEVENT_HANDLER_LINUX_RTSIG:
		if (0 != fdevent_linux_rtsig_init(ev)) {
			fprintf(stderr, "%s.%d: event-handler linux-rtsig failed, try to set server.event-handler = \"poll\" or \"select\"\n",
				__FILE__, __LINE__);
			return NULL;
		}
		break;
	case FDEVENT_HANDLER_LINUX_SYSEPOLL:
		if (0 != fdevent_linux_sysepoll_init(ev)) {
			fprintf(stderr, "%s.%d: event-handler linux-sysepoll failed, try to set server.event-handler = \"poll\" or \"select\"\n",
				__FILE__, __LINE__);
			return NULL;
		}
		break;
	case FDEVENT_HANDLER_SOLARIS_DEVPOLL:
		if (0 != fdevent_solaris_devpoll_init(ev)) {
			fprintf(stderr, "%s.%d: event-handler solaris-devpoll failed, try to set server.event-handler = \"poll\" or \"select\"\n",
				__FILE__, __LINE__);
			return NULL;
		}
		break;
	case FDEVENT_HANDLER_FREEBSD_KQUEUE:
		if (0 != fdevent_freebsd_kqueue_init(ev)) {
			fprintf(stderr, "%s.%d: event-handler freebsd-kqueue failed, try to set server.event-handler = \"poll\" or \"select\"\n",
				__FILE__, __LINE__);
			return NULL;
		}
		break;
	default:
		fprintf(stderr, "%s.%d: event-handler is unknown, try to set server.event-handler = \"poll\" or \"select\"\n",
			__FILE__, __LINE__);
		return NULL;
	}

	return ev;
}

void fdevent_free(fdevents *ev) {
	size_t i;
	if (!ev) return;

	if (ev->free) ev->free(ev);

	for (i = 0; i < ev->maxfds; i++) {
		if (ev->fdarray[i]) free(ev->fdarray[i]);
	}

	free(ev->fdarray);
	free(ev);
}

int fdevent_reset(fdevents *ev) {
	if (ev->reset) return ev->reset(ev);

	return 0;
}

fdnode *fdnode_init() {
	fdnode *fdn;

	fdn = calloc(1, sizeof(*fdn));
	fdn->fd = -1;

	return fdn;
}

void fdnode_free(fdnode *fdn) {
	free(fdn);
}

int fdevent_register(fdevents *ev, iosocket *sock, fdevent_handler handler, void *ctx) {
	fdnode *fdn;

	fdn = fdnode_init();
	fdn->handler = handler;
	fdn->fd      = sock->fd;
	fdn->ctx     = ctx;

	ev->fdarray[sock->fd] = fdn;

	return 0;
}

int fdevent_unregister(fdevents *ev, iosocket *sock) {
	fdnode *fdn;
        if (!ev) return 0;
	fdn = ev->fdarray[sock->fd];

	fdnode_free(fdn);

	ev->fdarray[sock->fd] = NULL;

	return 0;
}

int fdevent_event_del(fdevents *ev, iosocket *sock) {
	if (ev->event_del) ev->event_del(ev, sock);

	return 0;
}

int fdevent_event_add(fdevents *ev, iosocket *sock, int events) {
	if (ev->event_add) ev->event_add(ev, sock, events);

	return 0;
}

int fdevent_poll(fdevents *ev, int timeout_ms) {
	if (ev->poll == NULL) SEGFAULT();
	return ev->poll(ev, timeout_ms);
}

int fdevent_get_revents(fdevents *ev, size_t event_count, fdevent_revents *revents) {
	size_t i;

	if (ev->get_revents == NULL) SEGFAULT();

	fdevent_revents_reset(revents);

	ev->get_revents(ev, event_count, revents);

	/* patch the event handlers */
	for (i = 0; i < event_count; i++) {
		fdevent_revent *r = revents->ptr[i];

		r->handler = ev->fdarray[r->fd]->handler;
		r->context = ev->fdarray[r->fd]->ctx;
	}

	return 0;
}

int fdevent_fcntl_set(fdevents *ev, iosocket *sock) {
#ifdef _WIN32
	int i = 1;
#endif
#ifdef FD_CLOEXEC
	/* close fd on exec (cgi) */
	fcntl(sock->fd, F_SETFD, FD_CLOEXEC);
#endif
	if ((ev) && (ev->fcntl_set)) return ev->fcntl_set(ev, sock->fd);
#ifdef O_NONBLOCK
	return fcntl(sock->fd, F_SETFL, O_NONBLOCK | O_RDWR);
#elif defined _WIN32
	return ioctlsocket(sock->fd, FIONBIO, &i);
#else
	return 0;
#endif
}

