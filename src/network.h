#ifndef _NETWORK_H_
#define _NETWORK_H_

#include "server.h"
#include "network_backends.h"
#include "file_descr.h"

network_t network_write_chunkqueue(server *srv, file_descr *write_fd, chunkqueue *c);
network_t network_read_chunkqueue(server *srv, file_descr *read_fd, chunkqueue *c);

int network_init(server *srv);
int network_close(server *srv);

int network_register_fdevents(server *srv);

#endif
