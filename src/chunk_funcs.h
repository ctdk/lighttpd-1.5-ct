#ifndef _CHUNK_QUEUE_FUNCS_H_
#define _CHUNK_QUEUE_FUNCS_H_

#include "base.h"
#include "chunk.h"

/* for srv->cur_fds-- */
void chunkqueue_reset(server *srv, chunkqueue *c);
int chunkqueue_remove_empty_chunks(server *srv, chunkqueue *cq);

#endif
