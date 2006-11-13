#define _GNU_SOURCE /* we need O_DIRECT */

#include "network_backends.h"

#ifdef USE_LINUX_AIO_SENDFILE
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

#include <libaio.h>

#include "network.h"
#include "fdevent.h"
#include "log.h"
#include "stat_cache.h"
#include "joblist.h"

NETWORK_BACKEND_WRITE(linuxaiosendfile) {
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
			off_t offset;
			int rounds = 8; 

			offset = c->file.start + c->offset;

			/* open file if not already opened */
			if (-1 == c->file.fd) {
				if (-1 == (c->file.fd = open(c->file.name->ptr, O_RDONLY | O_DIRECT))) {
					log_error_write(srv, __FILE__, __LINE__, "ss", "open failed: ", strerror(errno));

					return -1;
				}
			
				if (offset) lseek(c->file.fd, offset, SEEK_SET);

#ifdef FD_CLOEXEC
				fcntl(c->file.fd, F_SETFD, FD_CLOEXEC);
#endif
			}

			do {
				size_t toSend;
				const size_t max_toSend = 2 * 256 * 1024; /** should be larger than the send buffer */

				toSend = c->file.length - c->offset > max_toSend ?
					max_toSend : c->file.length - c->offset;

				if (-1 == c->file.copy.fd || 0 == c->file.copy.length) {
        
					struct io_event event;
					struct iocb iocb;
			        	struct iocb *iocbs[] = { &iocb };
				        /* 30 second timeout should be enough */
				        struct timespec io_ts;
					int io_is_ready = 0;

					int res;
				
					c->file.copy.offset = 0; 
					c->file.copy.length = toSend;

					if (-1 == c->file.copy.fd) {
						char tmpfile_name[sizeof("/dev/shm/XXXXXX")];

						/* open a file in /dev/shm to write to */
						strcpy(tmpfile_name, "/dev/shm/XXXXXX");
						c->file.copy.fd = mkstemp(tmpfile_name);
						unlink(tmpfile_name); /* remove the file again, we still keep it open */
						ftruncate(c->file.copy.fd, toSend);

						c->file.mmap.offset = 0;
						c->file.mmap.length = toSend;
						c->file.mmap.start = mmap(0, c->file.mmap.length, PROT_READ | PROT_WRITE, MAP_SHARED, c->file.copy.fd, 0);
				
						assert(c->file.mmap.start != MAP_FAILED);
					}

					/* copy the 64k of the file to the tmp-file in shm */
					io_prep_pread(&iocb, c->file.fd, c->file.mmap.start, c->file.copy.length, 0);
					iocb.data = con;
        	
					assert(1 == io_submit(srv->linux_io_ctx, 1, iocbs));

					io_ts.tv_sec = 0;
				        io_ts.tv_nsec = 0;

					srv->linux_io_waiting++;
#if 0
					while (srv->linux_io_waiting && 1 == io_getevents(srv->linux_io_ctx, 0, 1, &event, &io_ts)) {
						connection *io_con = event.data;

						srv->linux_io_waiting--;

						if (io_con == con) {
							io_is_ready = 1;
							break;
						}

						/* this is not our connection, active the other one */
						joblist_append(srv, io_con);
					}
#endif
					if (!io_is_ready) return NETWORK_STATUS_WAIT_FOR_AIO_EVENT;
				} else if (c->file.copy.offset == 0) {
					/* we are finished */
				}

				offset = c->file.copy.offset;
				toSend = c->file.copy.length - offset;

				/* send the tmp-file from /dev/shm/ */
				if (-1 == (r = sendfile(sock->fd, c->file.copy.fd, &offset, toSend))) {
					switch (errno) {
					case EAGAIN:
					case EINTR:
						return NETWORK_STATUS_WAIT_FOR_EVENT;
					case EPIPE:
					case ECONNRESET:
						return NETWORK_STATUS_CONNECTION_CLOSE;
					default:
						log_error_write(srv, __FILE__, __LINE__, "ssd",
								"sendfile failed:", strerror(errno), sock->fd);
						return NETWORK_STATUS_FATAL_ERROR;
					}
				}

				if (r == 0) {
					/* ... ooops */
					return NETWORK_STATUS_CONNECTION_CLOSE;
				}

				c->offset += r; /* global offset in the file */
				cq->bytes_out += r;
				c->file.copy.offset += r; /* local offset in the mmap file */

				if (c->file.copy.offset == c->file.copy.length) {
					c->file.copy.length = 0; /* reset the length and let the next round fetch a new item */
				}

				if (c->offset == c->file.length) {
					chunk_finished = 1;
	
					if (c->file.copy.fd != -1) {
						close(c->file.copy.fd);
						c->file.copy.fd = -1;
					}

					if (c->file.fd != -1) {
						close(c->file.fd);
						c->file.fd = -1;
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
#if 0
network_linuxsendfile_init(void) {
	p->write = network_linuxsendfile_write_chunkset;
}
#endif
