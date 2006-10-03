#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "network.h"
#include "fdevent.h"
#include "log.h"
#include "stat_cache.h"

#include "sys-socket.h"
#include "sys-files.h"

#include "network_backends.h"

#ifdef USE_WIN32_SEND
/**
* fill the chunkqueue will all the data that we can get
*
* this might be optimized into a readv() which uses the chunks
* as vectors
*/

NETWORK_BACKEND_READ(win32recv) {
    int toread = 0;
    buffer *b;
    int r;

	/* check how much we have to read */
	if (ioctlsocket(fd, FIONREAD, &toread)) {
		log_error_write(srv, __FILE__, __LINE__, "sd",
				"ioctl failed: ",
				fd);
		return NETWORK_STATUS_FATAL_ERROR;
	}

	if (toread == 0) {
        /* win32 is strange */
        return con->bytes_read ? NETWORK_STATUS_CONNECTION_CLOSE : NETWORK_STATUS_WAIT_FOR_EVENT;
    }

    /*
    * our chunk queue is quiet large already
    *
    * let's buffer it to disk
    */

    b = chunkqueue_get_append_buffer(cq);

    buffer_prepare_copy(b, toread + 1);

    r = recv(fd, b->ptr, toread, 0);

    /* something went wrong */
    if (r < 0) {
        errno = WSAGetLastError();

        if (errno == WSAEWOULDBLOCK) return NETWORK_STATUS_WAIT_FOR_EVENT;
		if (errno == EINTR) {
			/* we have been interrupted before we could read */
			return NETWORK_STATUS_INTERRUPTED;
		}

		if (errno == WSAECONNRESET) {
            /* expected for keep-alive */
            return NETWORK_STATUS_CONNECTION_CLOSE;
        }

		log_error_write(srv, __FILE__, __LINE__, "ssdd",
            "connection closed - read failed: ",
            strerror(errno), con->fd, errno);

        return NETWORK_STATUS_FATAL_ERROR;
    }
	/* this should be catched by the b > 0 above */
	assert(r);
	b->used += r + 1;
	b->ptr[b->used - 1] = '\0';

    return NETWORK_STATUS_SUCCESS;
}

NETWORK_BACKEND_WRITE(win32send) {
	chunk *c;
	size_t chunks_written = 0;

	for(c = cq->first; c; c = c->next) {
		int chunk_finished = 0;

		switch(c->type) {
		case MEM_CHUNK: {
			char * offset;
			size_t toSend;
			ssize_t r;

			if (c->mem->used == 0) {
				chunk_finished = 1;
				break;
			}

			offset = c->mem->ptr + c->offset;
			toSend = c->mem->used - 1 - c->offset;

			if ((r = send(fd, offset, toSend, 0)) < 0) {
                    errno = WSAGetLastError();

                switch(errno) {
                case WSAEWOULDBLOCK:
                    return NETWORK_STATUS_WAIT_FOR_EVENT;
                case WSAECONNABORTED:
                case WSAECONNRESET:
                    return NETWORK_STATUS_CONNECTION_CLOSE;
                default:
                    log_error_write(srv, __FILE__, __LINE__, "sdd", "send to socket:", errno, fd);
                    return NETWORK_STATUS_FATAL_ERROR;
                }

				return NETWORK_STATUS_FATAL_ERROR;
			}

			c->offset += r;
			cq->bytes_out += r;

			if (c->offset == (off_t)c->mem->used - 1) {
				chunk_finished = 1;
			}

			break;
		}
		case FILE_CHUNK: {
			ssize_t r;
			off_t offset;

			stat_cache_entry *sce = NULL;

			if (HANDLER_ERROR == stat_cache_get_entry(srv, con, c->file.name, &sce)) {
				log_error_write(srv, __FILE__, __LINE__, "sb",
						strerror(errno), c->file.name);
				return NETWORK_STATUS_FATAL_ERROR;
			}

			offset = c->file.start + c->offset;

			if (offset > sce->st.st_size) {
				log_error_write(srv, __FILE__, __LINE__, "sb", "file was shrinked:", c->file.name);

				return NETWORK_STATUS_FATAL_ERROR;
			}


            if (-1 == c->file.fd) {
			    if (-1 == (c->file.fd = open(c->file.name->ptr, O_RDONLY|O_BINARY|O_SEQUENTIAL))) {
				    log_error_write(srv, __FILE__, __LINE__, "ss", "open failed: ", strerror(errno));

				    return NETWORK_STATUS_FATAL_ERROR;
			    }
            }

            if (-1 == lseek(c->file.fd, offset, SEEK_SET)) {
                log_error_write(srv, __FILE__, __LINE__, "ss", "lseek failed: ", strerror(errno));
            }

            while(1) {
                off_t haveRead = 0;
                int toSend;

                /* only send 64k blocks */
                toSend = c->file.length - c->offset > 256 * 1024 ? 256 * 1024 : c->file.length - c->offset;

		    	buffer_prepare_copy(srv->tmp_buf, toSend);

			    if (-1 == (haveRead = read(c->file.fd, srv->tmp_buf->ptr, toSend))) {
				    log_error_write(srv, __FILE__, __LINE__, "ss", "read from file: ", strerror(errno));


				    return NETWORK_STATUS_FATAL_ERROR;
			    }

			    if (-1 == (r = send(fd, srv->tmp_buf->ptr, haveRead, 0))) {
                    errno = WSAGetLastError();

                    switch(errno) {
                    case WSAEWOULDBLOCK:
                        return NETWORK_STATUS_WAIT_FOR_EVENT;
                    case WSAECONNABORTED:
                    case WSAECONNRESET:
                        return NETWORK_STATUS_CONNECTION_CLOSE;
                    default:
				        log_error_write(srv, __FILE__, __LINE__, "sd", "send to socket:", errno);

	    			    return NETWORK_STATUS_FATAL_ERROR;
                    }
		    	}

       			c->offset += r;
    			cq->bytes_out += r;

                if (r != haveRead) {
                    break;
                }
            }

			if (c->offset == c->file.length) {
				chunk_finished = 1;
			}

			break;
		}
		default:

			log_error_write(srv, __FILE__, __LINE__, "ds", c, "type not known");

			return NETWORK_STATUS_FATAL_ERROR;
		}

		if (!chunk_finished) {
			/* not finished yet */

			return NETWORK_STATUS_WAIT_FOR_EVENT;
		}

		chunks_written++;
	}
    fprintf(stderr, "%s.%d: chunks_written: %d\r\n", __FILE__, __LINE__, chunks_written);

	return NETWORK_STATUS_SUCCESS;
}

#endif
