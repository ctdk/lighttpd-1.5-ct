#ifndef _NETWORK_H_
#define _NETWORK_H_

#include "server.h"

network_status_t network_write_chunkqueue(server *srv, connection *con, chunkqueue *c);
network_status_t network_read(server *srv, connection *con, iosocket *sock, chunkqueue *c);

int network_init(server *srv);
int network_close(server *srv);

int network_register_fdevents(server *srv);
handler_t network_server_handle_fdevent(void *s, void *context, int revents);

#endif
