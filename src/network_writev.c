#include "network_backends.h"

#ifdef USE_WRITEV

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#include "network.h"
#include "fdevent.h"
#include "log.h"
#include "file_cache_funcs.h"

#ifndef UIO_MAXIOV
# if defined(__FreeBSD__) || defined(__APPLE__) || defined(__NetBSD__)
/* FreeBSD 4.7 defines it in sys/uio.h only if _KERNEL is specified */ 
#  define UIO_MAXIOV 1024
# elif defined(__sgi)
/* IRIX 6.5 has sysconf(_SC_IOV_MAX) which might return 512 or bigger */ 
#  define UIO_MAXIOV 512
# elif defined(__sun)
/* Solaris (and SunOS?) defines IOV_MAX instead */
#  ifndef IOV_MAX
#   define UIO_MAXIOV 16
#  else
#   define UIO_MAXIOV IOV_MAX
#  endif
# elif defined(IOV_MAX)
#  define UIO_MAXIOV IOV_MAX
# else
#  error UIO_MAXIOV nor IOV_MAX are defined
# endif
#endif

network_t network_write_chunkqueue_writev(server *srv, file_descr *write_fd, chunkqueue *cq) {
	chunk *c;
	
	for(c = cq->first; c; c = c->next) {
		int chunk_finished = 0;
		
		switch(c->type) {
		case MEM_CHUNK: {
			char * offset;
			size_t toSend;
			ssize_t r;
			
			size_t num_chunks, i;
			struct iovec chunks[UIO_MAXIOV];
			chunk *tc;
			size_t num_bytes = 0;
			
			/* we can't send more then SSIZE_MAX bytes in one chunk */
			
			/* build writev list 
			 * 
			 * 1. limit: num_chunks < UIO_MAXIOV
			 * 2. limit: num_bytes < SSIZE_MAX
			 */
			for(num_chunks = 0, tc = c; tc && tc->type == MEM_CHUNK && num_chunks < UIO_MAXIOV; num_chunks++, tc = tc->next);
			
			for(tc = c, i = 0; i < num_chunks; tc = tc->next, i++) {
				if (tc->data.mem->used == 0) {
					chunks[i].iov_base = tc->data.mem->ptr;
					chunks[i].iov_len  = 0;
				} else {
					offset = tc->data.mem->ptr + tc->offset;
					toSend = tc->data.mem->used - 1 - tc->offset;
				
					chunks[i].iov_base = offset;
					
					/* protect the return value of writev() */
					if (toSend > SSIZE_MAX ||
					    num_bytes + toSend > SSIZE_MAX) {
						chunks[i].iov_len = SSIZE_MAX - num_bytes;
						
						num_chunks = i + 1;
						break;
					} else {
						chunks[i].iov_len = toSend;
					}
					
					num_bytes += toSend;
				}
			}
			
			if ((r = writev(write_fd->fd, chunks, num_chunks)) < 0) {
				switch (errno) {
				case EAGAIN:
				case EINTR:
					r = 0;
					break;
				case EPIPE:
				case ECONNRESET:
					return NETWORK_REMOTE_CLOSE;
				default:
					log_error_write(srv, __FILE__, __LINE__, "ssd", 
							"writev failed:", strerror(errno), write_fd->fd);
				
					return NETWORK_ERROR;
				}
			}

			if (r != num_bytes) {
				write_fd->is_writable = 0;
			}
			
			/* check which chunks have been written */
			
			for(i = 0, tc = c; i < num_chunks; i++, tc = tc->next) {
				if (r >= (ssize_t)chunks[i].iov_len) {
					/* written */
					r -= chunks[i].iov_len;
					tc->offset += chunks[i].iov_len;
					write_fd->bytes_written += chunks[i].iov_len;
					
					if (chunk_finished) {
						/* skip the chunks from further touches */
						c = c->next;
					} else {
						chunk_finished++;
					}
				} else {
					/* partially written */
					
					tc->offset += r;
					write_fd->bytes_written += r;
					chunk_finished = 0;
#if 0					
					log_error_write(srv, __FILE__, __LINE__, "sdd", 
						"(debug) partially write: ", r, fd);
#endif
					break;
				}
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
			
			switch (file_cache_check_entry(srv, fce)) {
			case HANDLER_GO_ON:
				if (fce->st.st_size == 0 ||
				    fce->fd == -1) {

					log_error_write(srv, __FILE__, __LINE__, "sbdd", "foo", 
							c->data.file.fce->name, 
							fce->st.st_size, fce->fd);
				}
			
				offset = c->data.file.offset + c->offset;
				toSend = c->data.file.length - c->offset;
			
				if (offset > fce->st.st_size) {
					log_error_write(srv, __FILE__, __LINE__, "sb", 
							"file was shrinked:", 
							c->data.file.fce->name);
					
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
						log_error_write(srv, __FILE__, __LINE__, "ssbd", 
								"mmap failed: ", 
								strerror(errno), c->data.file.fce->name, fce->fd);
						
						return NETWORK_ERROR;
					}
					fce->mmap_p = p;
					fce->mmap_offset = 0;
					fce->mmap_length = fce->st.st_size;
				} else {
					p = fce->mmap_p;
				}
				
				if ((r = write(write_fd->fd, p + offset, toSend)) <= 0) {
					switch (errno) {
					case EAGAIN:
					case EINTR:
						r = 0;
						break;
					case EPIPE:
					case ECONNRESET:
						return NETWORK_REMOTE_CLOSE;
					default:
						log_error_write(srv, __FILE__, __LINE__, "ssd", 
								"write failed:", strerror(errno), write_fd->fd);
						
						return NETWORK_ERROR;
					}
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
					log_error_write(srv, __FILE__, __LINE__, "ss", 
							"read:", strerror(errno));
					
					return NETWORK_ERROR;
				}
				
				if (-1 == (r = write(write_fd->fd, srv->tmp_buf->ptr, toSend))) {
					log_error_write(srv, __FILE__, __LINE__, "ss", "write: ", strerror(errno));
					
					return NETWORK_ERROR;
				}
#endif
			
				if (r != toSend) {
					write_fd->is_readable = 0;
				}	
				
				break;
			case HANDLER_WAIT_FOR_FD:
				
				log_error_write(srv, __FILE__, __LINE__, "ssd", 
						"writev (handled):", strerror(errno), errno);
				
				r = 0;
				
				break;
			default:
				log_error_write(srv, __FILE__, __LINE__, "sb",
						strerror(errno), c->data.file.fce->name);
				return NETWORK_ERROR;
			}

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

#endif
