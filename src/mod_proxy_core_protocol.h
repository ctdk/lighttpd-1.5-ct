#ifndef _MOD_PROXY_CORE_PROTOCOL_H_
#define _MOD_PROXY_CORE_PROTOCOL_H_

#include "base.h"
#include "array-static.h"
#include "buffer.h"

#define SESSION_FUNC(x) \
		static int x(server *srv, proxy_session *sess)

#define STREAM_IN_OUT_FUNC(x) \
		static int x(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out)

typedef struct {
	buffer *name;

	int (*proxy_stream_init)            (server *srv, proxy_session *sess);
	int (*proxy_stream_cleanup)         (server *srv, proxy_session *sess);
	int (*proxy_stream_decoder)         (server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out);
	int (*proxy_stream_encoder)         (server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out);
	int (*proxy_parse_response_header)  (server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out);
	int (*proxy_get_request_chunk)      (server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out);

} proxy_protocol;

ARRAY_STATIC_DEF(proxy_protocols, proxy_protocol, );

proxy_protocol *proxy_protocol_init(void);
void proxy_protocol_free(proxy_protocol *protocol);

void proxy_protocols_init(void);
void proxy_protocols_free(void);
void proxy_protocols_register(proxy_protocol *protocol);
proxy_protocol *proxy_get_protocol(buffer *name);
const char *proxy_available_protocols(void);

#endif

