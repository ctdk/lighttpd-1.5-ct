#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include "log.h"
#include "file_cache_funcs.h"
#include "fdevent.h"
#include "etag.h"

#ifdef HAVE_ATTR_ATTRIBUTES_H
#include <attr/attributes.h>
#endif

#include "sys-mmap.h"

/* NetBSD 1.3.x needs it */
#ifndef MAP_FAILED
# define MAP_FAILED -1
#endif

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

#ifndef HAVE_LSTAT
#define lstat stat
#endif

/* don't enable the dir-cache 
 * 
 * F_NOTIFY would be nice but only works with linux-rtsig
 */
#undef USE_LINUX_SIGIO

file_cache *file_cache_init(void) {
	file_cache *fc = NULL;
	
	fc = calloc(1, sizeof(*fc));
	
	fc->dir_name = buffer_init();
	
	return fc;
}

static file_cache_entry * file_cache_entry_init(void) {
	file_cache_entry *fce = NULL;
	
	fce = calloc(1, sizeof(*fce));
	
	fce->fd = -1;
	fce->fde_ndx = -1;
	fce->name = buffer_init();
	fce->etag = buffer_init();
	fce->content_type = buffer_init();
	
	return fce;
}

static void file_cache_entry_free(server *srv, file_cache_entry *fce) {
	if (!fce) return;
	
	if (fce->fd >= 0) {
		close(fce->fd);
		srv->cur_fds--;
	}
	
	buffer_free(fce->etag);
	buffer_free(fce->name);
	buffer_free(fce->content_type);
	
	if (fce->mmap_p) munmap(fce->mmap_p, fce->mmap_length);
	
	free(fce);
}

static int file_cache_entry_reset(server *srv, file_cache_entry *fce) {
	if (fce->fd < 0) return 0;
	
	close(fce->fd);
	srv->cur_fds--;
			
#ifdef USE_LINUX_SIGIO
	/* doesn't work anymore */
	if (fce->fde_ndx != -1) {
		fdevent_event_del(srv->ev, &(fce->fde_ndx), fce->fd);
	}
#else 
	UNUSED(srv);
#endif
	
	if (fce->mmap_p) {
		munmap(fce->mmap_p, fce->mmap_length); 
		fce->mmap_p = NULL;
	}
	fce->fd = -1;
	
	buffer_reset(fce->etag);
	buffer_reset(fce->name);
	buffer_reset(fce->content_type);
	
	return 0;
}

void file_cache_free(server *srv, file_cache *fc) {
	size_t i;
	for (i = 0; i < fc->used; i++) {
		file_cache_entry_free(srv, fc->ptr[i]);
	}
	
	free(fc->ptr);
	
	buffer_free(fc->dir_name);
	
	free(fc);
	
}

#ifdef HAVE_XATTR
int fce_attr_get(buffer *buf, char *name) {
	int attrlen;
	int ret;
	
	attrlen = 1024;
	buffer_prepare_copy(buf, attrlen);
	attrlen--;
	if(0 == (ret = attr_get(name, "Content-Type", buf->ptr, &attrlen, 0))) {
		buf->used = attrlen + 1;
		buf->ptr[attrlen] = '\0';
	}
	return ret;
}
#endif

file_cache_entry * file_cache_get_unused_entry(server *srv) {
	file_cache_entry *fce = NULL;
	file_cache *fc = srv->file_cache;
	size_t i;
	
	if (fc->size == 0) {
		fc->size = 16;
		fc->ptr = calloc(fc->size, sizeof(*fc->ptr));
		fc->used = 0;
	}
	for (i = 0; i < fc->used; i++) {
		file_cache_entry *f = fc->ptr[i];

		if (f->fd == -1) {
			return f;
		}
	}
	
	if (fc->used < fc->size) {
		fce = file_cache_entry_init();
		fc->ptr[fc->used++] = fce;
	} else {
		/* the cache is full, time to resize */
		
		fc->size += 16;
		fc->ptr = realloc(fc->ptr, sizeof(*fc->ptr) * fc->size);
		
		fce = file_cache_entry_init();
		fc->ptr[fc->used++] = fce;
	}
	
	return fce;
}

file_cache_entry * file_cache_get_entry(server *srv, buffer *name) {
	file_cache *fc = srv->file_cache;
	size_t i;
	
	if (fc->size == 0) return NULL;

	for (i = 0; i < fc->used; i++) {
		file_cache_entry *f = fc->ptr[i];

		if (f->fd != -1 && 
		    buffer_is_equal(f->name, name)) {
			return f;
		}
	}
	
	return NULL;
}


handler_t file_cache_handle_fdevent(void *_srv, void *_fce, int revent) {
	size_t i;
	server *srv = _srv;
	file_cache_entry *fce = _fce;
	file_cache *fc = srv->file_cache;;

	UNUSED(revent);
	/* */
#if 0	
	log_error_write(srv, __FILE__, __LINE__, "sds", "dir has changed: ", fce->fd, fce->name->ptr);
#endif	
	/* touch all files below this directory */
	
	for (i = 0; i < fc->used; i++) {
		file_cache_entry *f = fc->ptr[i];
		
		if (fce == f) continue;
		
		if (0 == strncmp(fce->name->ptr, f->name->ptr, fce->name->used - 1)) {
#if 0
			log_error_write(srv, __FILE__, __LINE__, "ss", "file hit: ", f->name->ptr);
#endif
			f->is_dirty = 1;
		}
	}
	
	return HANDLER_GO_ON;
}

handler_t file_cache_check_entry(server *srv, file_cache_entry *fce) {
	struct stat st;
	/* no need to recheck */

	if (fce->stat_ts == srv->cur_ts) return HANDLER_GO_ON;

	if (-1 == (fce->follow_symlink ? stat(fce->name->ptr, &(st)) : lstat(fce->name->ptr, &(st)))) {
		int oerrno = errno;

		file_cache_entry_reset(srv, fce);
		
		errno = oerrno;
		return HANDLER_ERROR;
	}
	
	fce->stat_ts = srv->cur_ts;

	/* still the same file */
	if (st.st_mtime == fce->st.st_mtime &&
	    st.st_size == fce->st.st_size &&
	    st.st_ino == fce->st.st_ino) return HANDLER_GO_ON;

	fce->st = st;
	
	if (S_ISREG(fce->st.st_mode)) {
		if (fce->fd != -1) {
			/* reopen file */
			close(fce->fd);
			srv->cur_fds--;
		}
		
		if (-1 == (fce->fd = open(fce->name->ptr, O_RDONLY | O_LARGEFILE))) {
			int oerrno = errno;
			if (errno == EMFILE || errno == EINTR) {
				return HANDLER_WAIT_FOR_FD;
			}
			
			log_error_write(srv, __FILE__, __LINE__, "sbs", 
					"open failed for:", fce->name, 
					strerror(errno));
			
			buffer_reset(fce->name);

			errno = oerrno;
			return HANDLER_ERROR;
		}
	
		srv->cur_fds++;	
		etag_create(fce->etag, &(fce->st));
	}

	return HANDLER_GO_ON;
}

handler_t file_cache_add_entry(server *srv, connection *con, buffer *name, file_cache_entry **fce_ptr) {
	file_cache_entry *fce = NULL;
	handler_t ret;

	fce = file_cache_get_unused_entry(srv);
	
	buffer_copy_string_buffer(fce->name, name);
	fce->in_use = 0;
	fce->fd = -1;
	fce->follow_symlink = con->conf.follow_symlink;
	fce->stat_ts = 0;

	if (HANDLER_GO_ON != (ret = file_cache_check_entry(srv, fce))) {
		return ret;
	}

	if (S_ISREG(fce->st.st_mode)) {
		size_t k, s_len;

		/* determine mimetype */
		buffer_reset(fce->content_type);
		
		s_len = name->used - 1;
		
		for (k = 0; k < con->conf.mimetypes->used; k++) {
			data_string *ds = (data_string *)con->conf.mimetypes->data[k];
			size_t ct_len;
			
			if (ds->key->used == 0) continue;
			
			ct_len = ds->key->used - 1;
			
			if (s_len < ct_len) continue;
			
			if (0 == strncmp(name->ptr + s_len - ct_len, ds->key->ptr, ct_len)) {
				buffer_copy_string_buffer(fce->content_type, ds->value);
				break;
			}
		}
		
#ifdef HAVE_XATTR
		if (buffer_is_empty(fce->content_type)) {
			fce_attr_get(fce->content_type, name->ptr);
		}
#endif
	}

	fce->in_use++;
	
	*fce_ptr = fce;
	
	return HANDLER_GO_ON;
}

int file_cache_release_entry(server *srv, file_cache_entry *fce) {
	if (fce->in_use > 0) fce->in_use--;
	file_cache_entry_reset(srv, fce);
	
	return 0;
}

