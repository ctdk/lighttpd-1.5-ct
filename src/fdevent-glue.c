#include <sys/types.h>

#include "settings.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

#include "fdevent.h"
#include "buffer.h"

fdnode *fdnode_init() {
	fdnode *fdn;
	
	fdn = calloc(1, sizeof(*fdn));
	fdn->fd = -1;
	return fdn;
}

void fdnode_free(fdnode *fdn) {
	free(fdn);
}

int fdevent_register(fdevents *ev, file_descr *fd, fdevent_handler handler, void *ctx) {
	fdnode *fdn;
	
	fdn = fdnode_init();
	fdn->handler = handler;
	fdn->fd      = fd->fd;
	fdn->ctx     = ctx;
	
	ev->fdarray[fd->fd] = fdn;

	return 0;
}

int fdevent_unregister(fdevents *ev, file_descr *fd) {
	fdnode *fdn;
        if (!ev) return 0;
	fdn = ev->fdarray[fd->fd];
	
	fdnode_free(fdn);
	
	ev->fdarray[fd->fd] = NULL;
	
	return 0;
}

int fdevent_event_del(fdevents *ev, file_descr *fd) {
	if (ev->event_del) fd->fde_ndx = ev->event_del(ev, fd->fde_ndx, fd->fd);
	
	return 0;
}

int fdevent_event_add(fdevents *ev, file_descr *fd, int events) {
	if (ev->event_add) fd->fde_ndx = ev->event_add(ev, fd->fde_ndx, fd->fd, events);
	
	return 0;
}

int fdevent_fcntl_set(fdevents *ev, file_descr *fd) {
#ifdef FD_CLOEXEC
	/* close fd on exec (cgi) */
	fcntl(fd->fd, F_SETFD, FD_CLOEXEC);
#endif
	if (ev->fcntl_set) return ev->fcntl_set(ev, fd->fd);
#ifdef O_NONBLOCK	
	return fcntl(fd->fd, F_SETFL, O_NONBLOCK | O_RDWR);
#else
	return 0;
#endif
}

