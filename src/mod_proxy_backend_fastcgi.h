#ifndef _MOD_PROXY_BACKEND_FASTCGI_H_
#define _MOD_PROXY_BACKEND_FASTCGI_H_

#include "mod_proxy_core.h"
#include "base.h"

int proxy_fastcgi_stream_decoder(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out);
int proxy_fastcgi_stream_encoder(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out);

parse_status_t proxy_fastcgi_parse_response_header(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq);
int proxy_fastcgi_get_request_chunk(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq);

#endif
