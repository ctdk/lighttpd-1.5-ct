#include "network_backends.h"

#ifdef USE_OPENSSL
#include <sys/types.h>
#include <sys/socket.h>
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

#include "network.h"
#include "fdevent.h"
#include "log.h"
#include "file_cache_funcs.h"

#include <openssl/ssl.h> 
#include <openssl/err.h> 

network_t network_read_chunkqueue_openssl(server *srv, file_descr *read_fd, chunkqueue *cq) {
	buffer *b;
	ssize_t len;

	b = chunkqueue_get_append_buffer(cq);
	buffer_prepare_copy(b, 4096);

	if ((len = SSL_read(read_fd->ssl, b->ptr, b->size - 1)) < 0) {
		int r;
	
		switch ((r = SSL_get_error(read_fd->ssl, len))) {
		case SSL_ERROR_WANT_READ:
			read_fd->is_readable = 0;
			return NETWORK_OK;
		case SSL_ERROR_SYSCALL:
			switch(errno) {
			default:
				log_error_write(srv, __FILE__, __LINE__, "sddds", "SSL:", 
						len, r, errno,
						strerror(errno));
				break;
			}
			return NETWORK_ERROR;	
		case SSL_ERROR_ZERO_RETURN:
			/* clean shutdown on the remote side */
			
			return NETWORK_REMOTE_CLOSE;
		default:
			log_error_write(srv, __FILE__, __LINE__, "sds", "SSL:", 
					r, ERR_error_string(ERR_get_error(), NULL));
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


network_t network_write_chunkqueue_openssl(server *srv, file_descr *write_fd, chunkqueue *cq) {
	int ssl_r;
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
			
			/**
			 * SSL_write man-page
			 * 
			 * WARNING
			 *        When an SSL_write() operation has to be repeated because of
			 *        SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be
			 *        repeated with the same arguments.
			 * 
			 */
			
			if ((r = SSL_write(write_fd->ssl, offset, toSend)) <= 0) {
				int errcode;
				switch ((ssl_r = SSL_get_error(write_fd->ssl, r))) {
				case SSL_ERROR_WANT_WRITE:
					break;
				case SSL_ERROR_SYSCALL:
					if (0 != (errcode = ERR_get_error())) {
						log_error_write(srv, __FILE__, __LINE__, "sdddss", "SSL:", 
								ssl_r, r, errno,
								strerror(errno), ERR_error_string(errcode, NULL));
						return NETWORK_ERROR;
					}

					if (r == 0) {
						/* unexpected EOF */

						return NETWORK_REMOTE_CLOSE;
					} else if (r == -1) {
						switch(errno) {
						case EPIPE:
							return NETWORK_REMOTE_CLOSE;
						default:
							log_error_write(srv, __FILE__, __LINE__, "sddds", "SSL:", 
									ssl_r, r, errno,
									strerror(errno));
							break;
						}
					}
					
					return NETWORK_ERROR;
				case SSL_ERROR_ZERO_RETURN:
					/* clean shutdown on the remote side */
					
					if (r == 0) return NETWORK_REMOTE_CLOSE;
					
					/* fall thourgh */
				default:
					log_error_write(srv, __FILE__, __LINE__, "sdds", "SSL:", 
							ssl_r, r,
							ERR_error_string(ERR_get_error(), NULL));
					
					return NETWORK_ERROR;
				}
			} else {
				c->offset += r;
				write_fd->bytes_written += r;
			}
			
			if (c->offset == (off_t)c->data.mem->used - 1) {
				chunk_finished = 1;
			}
			
			break;
		}
		case FILE_CHUNK: {
			char *s;
			ssize_t r;
			off_t offset;
			size_t toSend;
# if defined USE_MMAP
			char *p;
# endif
			file_cache_entry *fce = c->data.file.fce;
			
			if (HANDLER_GO_ON != file_cache_check_entry(srv, fce)) {
				log_error_write(srv, __FILE__, __LINE__, "sb",
						strerror(errno), c->data.file.fce->name);
				return -1;
			}
			
			offset = c->data.file.offset + c->offset;
			toSend = c->data.file.length - c->offset;
			
			
#if defined USE_MMAP
			if (MAP_FAILED == (p = mmap(0, fce->st.st_size, PROT_READ, MAP_SHARED, fce->fd, 0))) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "mmap failed: ", strerror(errno));
				
				return -1;
			}
			
			s = p + offset;
#else
			buffer_prepare_copy(srv->tmp_buf, toSend);
			
			lseek(con->fce->fd, offset, SEEK_SET);
			if (-1 == (toSend = read(fce->fd, srv->tmp_buf->ptr, toSend))) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "read failed: ", strerror(errno));
				
				return -1;
			}
			
			s = srv->tmp_buf->ptr;
#endif
			
			if ((r = SSL_write(write_fd->ssl, s, toSend)) <= 0) {
				switch ((ssl_r = SSL_get_error(write_fd->ssl, r))) {
				case SSL_ERROR_WANT_WRITE:
					break;
				case SSL_ERROR_SYSCALL:
					switch(errno) {
					case EPIPE:
						return NETWORK_REMOTE_CLOSE;
					default:
						log_error_write(srv, __FILE__, __LINE__, "sddds", "SSL:", 
								ssl_r, r, errno,
								strerror(errno));
						break;
					}
					
					return  -1;
				case SSL_ERROR_ZERO_RETURN:
					/* clean shutdown on the remote side */
					
					if (r == 0) {
#if defined USE_MMAP
						munmap(p, c->data.file.length);
#endif
						return NETWORK_REMOTE_CLOSE;
					}
					
					/* fall thourgh */
				default:
					log_error_write(srv, __FILE__, __LINE__, "sdds", "SSL:", 
							ssl_r, r, 
							ERR_error_string(ERR_get_error(), NULL));
					
#if defined USE_MMAP
					munmap(p, c->data.file.length);
#endif
					return NETWORK_ERROR;
				}
			} else {
				c->offset += r;
				write_fd->bytes_written += r;
			}
			
#if defined USE_MMAP
			munmap(p, c->data.file.length);
#endif
			
			if (c->offset == c->data.file.length) {
				chunk_finished = 1;
			}
			
			break;
		}
		default:
			log_error_write(srv, __FILE__, __LINE__, "s", "type not known");
			
			return -1;
		}
			
		if (!chunk_finished) {
			/* not finished yet */
			
			break;
		}
	}
	
	return NETWORK_OK;
}
#endif

#if 0
network_openssl_init(void) {
	p->write_ssl = network_openssl_write_chunkset;
}
#endif
