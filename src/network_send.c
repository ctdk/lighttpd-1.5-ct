#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "network.h"
#include "fdevent.h"
#include "log.h"
#include "file_cache_funcs.h"

#include "sys-socket.h"

#include "network_backends.h"

network_t network_read_chunkqueue_send(server *srv, file_descr *read_fd, chunkqueue *cq) {
	int to_read;
	buffer *b;
	ssize_t len;
	/* 
	 * check how much we have to read 
	 */
	if (ioctl(read_fd->fd, FIONREAD, &to_read)) {
		log_error_write(srv, __FILE__, __LINE__, "sd", 
				"unexpected end-of-file (perhaps the fastcgi process died):",
				read_fd->fd);
		return NETWORK_ERROR;
	}

	b = chunkqueue_get_append_buffer(cq);
	buffer_prepare_copy(b, to_read);


	if (-1 == (len = recv(read_fd->fd, b->ptr, b->size - 1, 0))) {
		switch(errno) {
		case EAGAIN:
			/* wait for event */
			read_fd->is_readable = 0;
			return NETWORK_OK;
		case EINTR:
			return NETWORK_OK;
		case ECONNRESET:
			/* keep-alive, closed on client-side */
			return NETWORK_REMOTE_CLOSE;
		default: 
			log_error_write(srv, __FILE__, __LINE__, "ssd", "connection closed - read failed: ", strerror(errno), errno);
			return NETWORK_ERROR;
		}

	} else if (len == 0) {
		read_fd->is_readable = 0;
		/* the other end close the connection -> KEEP-ALIVE */

		return NETWORK_REMOTE_CLOSE;
	} else if ((size_t)len < b->size - 1) {
		/* we got less then expected, wait for the next fd-event */
		
		read_fd->is_readable = 0;
	}
	
	b->used = len;
	b->ptr[b->used++] = '\0';
	
	read_fd->bytes_read += len;

	return NETWORK_OK;
}

network_t network_write_chunkqueue_send(server *srv, file_descr *write_fd, chunkqueue *cq) {
	chunk *c;
	
	for(c = cq->first; c; c = c->next) {
		int chunk_finished = 0;
		
		switch(c->type) {
		case MEM_CHUNK: {
			char * offset;
			size_t toSend;
			ssize_t r;
			
			if (c->data.mem->used == 0) {
				chunk_finished = 1;
				break;
			}
			
			offset = c->data.mem->ptr + c->offset;
			toSend = c->data.mem->used - 1 - c->offset;

			if ((r = send(write_fd->fd, offset, toSend, 0)) < 0) {
				log_error_write(srv, __FILE__, __LINE__, "ssd", "write failed: ", strerror(errno), write_fd->fd);
				
				return NETWORK_ERROR;
			}
			
			c->offset += r;
			write_fd->bytes_written += r;
			
			if (c->offset == (off_t)c->data.mem->used - 1) {
				chunk_finished = 1;
			}
			
			break;
		}
		case FILE_CHUNK: {
#ifdef USE_MMAP
			char *p = NULL;
#endif
			ssize_t r;
			off_t offset;
			size_t toSend;
			file_cache_entry *fce = c->data.file.fce;
			
			if (HANDLER_GO_ON != file_cache_check_entry(srv, fce)) {
				log_error_write(srv, __FILE__, __LINE__, "sb",
						strerror(errno), fce->name);
				return NETWORK_ERROR;
			}
			
			offset = c->data.file.offset + c->offset;
			toSend = c->data.file.length - c->offset;
			
			if (offset > fce->st.st_size) {
				log_error_write(srv, __FILE__, __LINE__, "sb", "file was shrinked:", 
						fce->name);
				
				return NETWORK_ERROR;
			}
			
			if (-1 == fce->fd) {
				log_error_write(srv, __FILE__, __LINE__, "sb", "fd is invalid", 
						fce->name);
				
				return NETWORK_ERROR;
			}
			
#if defined USE_MMAP
			/* check if the mapping fits */
			if (fce->mmap_p &&
			    fce->mmap_length != fce->st.st_size &&
			    fce->mmap_offset != 0) {
				munmap(fce->mmap_p, fce->mmap_length);
				
				fce->mmap_p = NULL;
			}
			
			/* build mapping if neccesary */
			if (fce->mmap_p == NULL) {
				if (MAP_FAILED == (p = mmap(0, fce->st.st_size, PROT_READ, MAP_SHARED, fce->fd, 0))) {
					log_error_write(srv, __FILE__, __LINE__, "ss", "mmap failed: ", strerror(errno));
					
					return NETWORK_ERROR;
				}
				fce->mmap_p = p;
				fce->mmap_offset = 0;
				fce->mmap_length = fce->st.st_size;
			} else {
				p = fce->mmap_p;
			}
			
			if ((r = send(write_fd->fd, p + offset, toSend, 0)) <= 0) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "write failed: ", strerror(errno));
				
				return NETWORK_ERROR;
			}
			
			/* don't cache mmap()ings for files large then 64k */
			if (fce->mmap_length > 64 * 1024) {
				munmap(fce->mmap_p, fce->mmap_length);
				
				fce->mmap_p = NULL;
			}
			
#else
			buffer_prepare_copy(srv->tmp_buf, toSend);
			
			lseek(fce->fd, offset, SEEK_SET);
			if (-1 == (toSend = read(fce->fd, srv->tmp_buf->ptr, toSend))) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "read: ", strerror(errno));
				
				return NETWORK_ERROR;
			}

			if (-1 == (r = send(write_fd->fd, srv->tmp_buf->ptr, toSend, 0))) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "write: ", strerror(errno));
				
				return NETWORK_ERROR;
			}
#endif
			c->offset += r;
			write_fd->bytes_written += r;
			
			if (c->offset == c->data.file.length) {
				chunk_finished = 1;
			}
			
			break;
		}
		default:
			
			log_error_write(srv, __FILE__, __LINE__, "ds", c, "type not known");
			
			return NETWORK_ERROR;
		}
		
		if (!chunk_finished) {
			/* not finished yet */
			
			break;
		}
	}

	return NETWORK_OK;
}

#if 0
network_write_init(void) {
	p->write = network_write_write_chunkset;
}
#endif
