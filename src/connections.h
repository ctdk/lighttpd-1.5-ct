#ifndef _CONNECTIONS_H_
#define _CONNECTIONS_H_

#include "settings.h"
#include "server.h"
#include "fdevent.h"

LI_EXPORT connection* connection_init(server *srv);
LI_EXPORT int connection_reset(server *srv, connection *con);
LI_EXPORT void connections_free(server *srv);

LI_EXPORT connection* connection_accept(server *srv, server_socket *srv_sock);
LI_EXPORT int connection_close(server *srv, connection *con);

LI_EXPORT int connection_set_state(server *srv, connection *con, connection_state_t state);
LI_EXPORT const char * connection_get_state(connection_state_t state);
LI_EXPORT const char * connection_get_short_state(connection_state_t state);
LI_EXPORT int connection_state_machine(server *srv, connection *con);

#endif
