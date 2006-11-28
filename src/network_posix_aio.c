#define _GNU_SOURCE /* we need O_DIRECT */

#include "network_backends.h"

#ifdef USE_POSIX_AIO
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
#include <fcntl.h>
#include <assert.h>

#include <aio.h>

#include "network.h"
#include "fdevent.h"
#include "log.h"
#include "stat_cache.h"
#include "joblist.h"

#include "sys-files.h"

NETWORK_BACKEND_WRITE(posixaio) {
	chunk *c, *tc;
	size_t chunks_written = 0;

	for(c = cq->first; c; c = c->next, chunks_written++) {
		int chunk_finished = 0;
		network_status_t ret;

		switch(c->type) {
		case MEM_CHUNK:
			ret = network_write_chunkqueue_writev_mem(srv, con, sock, cq, c);

			/* check which chunks are finished now */
			for (tc = c; tc; tc = tc->next) {
				/* finished the chunk */
				if (tc->offset == tc->mem->used - 1) {
					/* skip the first c->next as that will be done by the c = c->next in the other for()-loop */
					if (chunk_finished) {
						c = c->next;
					} else {
						chunk_finished = 1;
					}
				} else {
					break;
				}
			}

			if (ret != NETWORK_STATUS_SUCCESS) {
				return ret;
			}

			break;
		case FILE_CHUNK: {
			ssize_t r;
			int rounds = 8; 

			/* open file if not already opened */
			if (-1 == c->file.fd) {
				if (-1 == (c->file.fd = open(c->file.name->ptr, O_RDONLY | /* O_DIRECT | */ (srv->srvconf.use_noatime ? O_NOATIME : 0)))) {
					ERROR("opening '%s' failed: %s", BUF_STR(c->file.name), strerror(errno));

					return NETWORK_STATUS_FATAL_ERROR;
				}

				srv->cur_fds++;
			
#ifdef FD_CLOEXEC
				fcntl(c->file.fd, F_SETFD, FD_CLOEXEC);
#endif
			}

			do {
				size_t toSend;
				const size_t max_toSend = 2 * 256 * 1024; /** should be larger than the send buffer */
				off_t offset;
			
				offset = c->file.start + c->offset;

				toSend = c->file.length - c->offset > max_toSend ?
					max_toSend : c->file.length - c->offset;

				if (0 == c->file.copy.length) {
					int res;
					int async_error = 0;

					size_t iocb_ndx;
					

					c->file.copy.offset = 0; 
					c->file.copy.length = toSend;

					if (c->file.copy.length == 0) {
						async_error = 1;
					}

					/* if we reused the previous tmp-file we get overlaps 
					 * 
					 *    1 ... 3904 are ok
					 * 3905 ... 4096 are replaces by 8001 ... 8192 
					 *
					 * somehow the second read writes into the mmap() before 
					 * the sendfile is finished which is very strange.
					 *
					 * if someone finds the reason for this, feel free to remove
					 * this if again and number of reduce the syscalls a bit.
					 */
					if (-1 != c->file.copy.fd) {
						munmap(c->file.mmap.start, c->file.mmap.length);

						close(c->file.copy.fd);
						c->file.copy.fd = -1;
					}

					/* do we have a IOCB we can use ? */

					for (iocb_ndx = 0; async_error == 0 && iocb_ndx < POSIX_AIO_MAX_IOCBS; iocb_ndx++) {
						if (NULL == srv->posix_aio_data[iocb_ndx]) {
							break;
						}
					}

					if (iocb_ndx == POSIX_AIO_MAX_IOCBS) {
						async_error = 1;
					}


					/* get mmap()ed mem-block in /dev/shm
					 *
					 * in case we don't have a iocb available, we still need the mmap() for the blocking
					 * read()
					 *  */
					if (-1 == c->file.copy.fd ) {
						int mmap_fd = -1;

						/* open a file in /dev/shm to write to */
						if (-1 == (mmap_fd = open("/dev/zero", O_RDWR))) {
							async_error = 1;
					
							if (errno != EMFILE) {	
								TRACE("open(/dev/zero) returned: %d (%s), open fds: %d, falling back to sync-io", 
									errno, strerror(errno), srv->cur_fds);
							}
						} else {
							c->file.mmap.offset = 0;
							c->file.mmap.length = c->file.copy.length; /* align to page-size */

							c->file.mmap.start = mmap(0, c->file.mmap.length, 
									PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
							if (c->file.mmap.start == MAP_FAILED) {
								async_error = 1;
							}

							close(mmap_fd);
							mmap_fd = -1;
						}
					}


					/* looks like we couldn't get a temp-file [disk-full] */	
					if (async_error == 0 && c->file.mmap.start != MAP_FAILED) {
						struct aiocb *iocb = NULL;

						assert(c->file.copy.length > 0);

						iocb = &srv->posix_aio_iocbs[iocb_ndx];

						memset(iocb, 0, sizeof(*iocb));

						iocb->aio_fildes = c->file.fd;
						iocb->aio_buf = c->file.mmap.start;
						iocb->aio_nbytes = c->file.copy.length;
						iocb->aio_offset = c->file.start + c->offset;

					       	if (0 == aio_read(iocb)) {
							srv->have_aio_waiting++;

							srv->posix_aio_iocbs_watch[iocb_ndx] = iocb;
							srv->posix_aio_data[iocb_ndx] = con;

							return NETWORK_STATUS_WAIT_FOR_AIO_EVENT;
						} else {
							srv->posix_aio_data[iocb_ndx] = NULL;

							if (errno != EAGAIN) {
								TRACE("aio_read returned: %d (%s), waiting jobs: %d, falling back to sync-io", 
									errno, strerror(errno), srv->have_aio_waiting);
							} else {
								TRACE("aio_read returned EAGAIN on (%d - %d), -> sync-io", c->file.fd, c->file.copy.fd);
							}
						}
					}

					/* fall back to a blocking read */

					if (c->file.mmap.start != MAP_FAILED) {
						lseek(c->file.fd, c->file.start + c->offset, SEEK_SET);

						if (-1 == (r = read(c->file.fd, c->file.mmap.start, c->file.copy.length))) {
							switch(errno) {
							default:
								ERROR("reading file failed: %d (%s)", errno, strerror(errno));

								return NETWORK_STATUS_FATAL_ERROR;
							}
						}

						if (r == 0) {
							ERROR("read() returned 0 ... not good: %s", "");

							return NETWORK_STATUS_FATAL_ERROR;
						}

						if (r != c->file.copy.length) {
							ERROR("read() returned %d instead of %d", r, c->file.copy.length);

							return NETWORK_STATUS_FATAL_ERROR;
						}
					} else {
						ERROR("the mmap() failed, no way for a fallback: %s", "");

						return NETWORK_STATUS_FATAL_ERROR;
					}

				} else if (c->file.copy.offset == 0) {
#if 0
					/**
					 * aio_write only creates extra-trouble
					 *
					 * instead we use the classic non-blocking-io write() call on the socket
					 */
					size_t iocb_ndx;
					struct aiocb *iocb = NULL;

					/* the aio_read() is finished, send it */

					/* do we have a IOCB we can use ? */

					for (iocb_ndx = 0; iocb_ndx < POSIX_AIO_MAX_IOCBS; iocb_ndx++) {
						if (NULL == srv->posix_aio_data[iocb_ndx]) {
							break;
						}
					}

					assert(iocb_ndx != POSIX_AIO_MAX_IOCBS);

					iocb = &srv->posix_aio_iocbs[iocb_ndx];
					memset(iocb, 0, sizeof(*iocb));

					iocb->aio_fildes = sock->fd;
					iocb->aio_buf = c->file.mmap.start;
					iocb->aio_nbytes = c->file.copy.length;
					iocb->aio_offset = 0;

					/* the write should only return when it is finished */
					fcntl(sock->fd, F_SETFL, fcntl(sock->fd, F_GETFL) & ~O_NONBLOCK);
					
					if (0 != aio_write(iocb)) { 
						TRACE("aio-write failed: %d (%s)", errno, strerror(errno));

						return NETWORK_STATUS_FATAL_ERROR;
					}

					srv->have_aio_waiting++;

					srv->posix_aio_iocbs_watch[iocb_ndx] = iocb;
					srv->posix_aio_data[iocb_ndx] = con;

					/* in case we come back: we have written everything */
					c->file.copy.offset =  c->file.copy.length;

					return NETWORK_STATUS_WAIT_FOR_AIO_EVENT;
#endif
				}

				if (-1 == (r = write(sock->fd, c->file.mmap.start + c->file.copy.offset, c->file.copy.length - c->file.copy.offset))) {
					switch (errno) {
					case EINTR:
					case EAGAIN:
						return NETWORK_STATUS_WAIT_FOR_EVENT;
					case ECONNRESET:
						return NETWORK_STATUS_CONNECTION_CLOSE;
					default:
						ERROR("write failed: %d (%s)", errno, strerror(errno));
						return NETWORK_STATUS_FATAL_ERROR;
					}
				}

				if (r == 0) {
					return NETWORK_STATUS_CONNECTION_CLOSE;
				}

				c->file.copy.offset += r; /* offset in the copy-chunk */

				c->offset += r; /* global offset in the file */
				cq->bytes_out += r;

				if (c->file.mmap.length == c->file.copy.offset) {
					munmap(c->file.mmap.start, c->file.mmap.length);
					c->file.mmap.start = MAP_FAILED;
					c->file.copy.length = 0;
				}
#if 0
				/* return as soon as possible again */
				fcntl(sock->fd, F_SETFL, fcntl(sock->fd, F_GETFL) | O_NONBLOCK);
#endif

				if (c->offset == c->file.length) {
					chunk_finished = 1;
	
					if (c->file.copy.fd != -1) {
						close(c->file.copy.fd);
						c->file.copy.fd = -1;
						srv->cur_fds--;
					}

					if (c->file.fd != -1) {
						close(c->file.fd);
						c->file.fd = -1;
						srv->cur_fds--;
					}
				}

				/* the chunk is larger and the current snippet is finished */
			} while (c->file.copy.length == 0 && chunk_finished == 0 && rounds-- > 0);

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
	}

	return NETWORK_STATUS_SUCCESS;
}

#endif

