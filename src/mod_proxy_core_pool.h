#ifndef _MOD_PROXY_CORE_POOL_H_
#define _MOD_PROXY_CORE_POOL_H_

#include <sys/time.h>

#include "iosocket.h"
#include "array-static.h"
#include "mod_proxy_core_address.h"

typedef enum {
	PROXY_CONNECTION_STATE_UNSET,
	PROXY_CONNECTION_STATE_CONNECTING,
	PROXY_CONNECTION_STATE_CONNECTED,
	PROXY_CONNECTION_STATE_IDLE,
	PROXY_CONNECTION_STATE_CLOSED,
} proxy_connection_state_t;

/**
 * a connection to a proxy backend
 * 
 * the connection is independent of the incoming request to allow keep-alive
 */
typedef struct { 
	iosocket *sock;

	time_t last_read; /* timeout handling for keep-alive connections */
	time_t last_write;

	proxy_address *address; /* the struct sock_addr for the sock */

	proxy_connection_state_t state;
} proxy_connection;

ARRAY_STATIC_DEF(proxy_connection_pool, proxy_connection, size_t max_size;);

typedef enum {
	PROXY_CONNECTIONPOOL_UNSET,
	PROXY_CONNECTIONPOOL_FULL,
	PROXY_CONNECTIONPOOL_GOT_CONNECTION,
} proxy_connection_pool_t;

proxy_connection_pool *proxy_connection_pool_init(void); 
void proxy_connection_pool_free(proxy_connection_pool *pool); 

proxy_connection_pool_t proxy_connection_pool_get_connection(proxy_connection_pool *pool, proxy_address *address, proxy_connection **rcon);
int proxy_connection_pool_remove_connection(proxy_connection_pool *pool, proxy_connection *c);

proxy_connection * proxy_connection_init(void);
void proxy_connection_free(proxy_connection *pool);

#endif

