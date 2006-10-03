#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

#include "buffer.h"
#include "array.h"
#include "log.h"

#include "base.h"
#include "plugin.h"
#include "joblist.h"
#include "sys-files.h"
#include "inet_ntop_cache.h"
#include "crc32.h"
#include "configfile.h"

#include "mod_proxy_core.h"
#include "mod_proxy_backend_http.h"
#include "mod_proxy_backend_fastcgi.h"

#define CONFIG_PROXY_CORE_BALANCER "proxy-core.balancer"
#define CONFIG_PROXY_CORE_PROTOCOL "proxy-core.protocol"
#define CONFIG_PROXY_CORE_DEBUG "proxy-core.debug"
#define CONFIG_PROXY_CORE_BACKENDS "proxy-core.backends"
#define CONFIG_PROXY_CORE_REWRITE_REQUEST "proxy-core.rewrite-request"
#define CONFIG_PROXY_CORE_REWRITE_RESPONSE "proxy-core.rewrite-response"
#define CONFIG_PROXY_CORE_ALLOW_X_SENDFILE "proxy-core.allow-x-sendfile"
#define CONFIG_PROXY_CORE_ALLOW_X_REWRITE "proxy-core.allow-x-rewrite"
#define CONFIG_PROXY_CORE_MAX_POOL_SIZE "proxy-core.max-pool-size"

int array_insert_int(array *a, const char *key, int val) {
	data_integer *di;

	if (NULL == (di = (data_integer *)array_get_unused_element(a, TYPE_INTEGER))) {
		di = data_integer_init();
	}

	buffer_copy_string(di->key, key);
	di->value = val;
	array_insert_unique(a, (data_unset *)di);

	return 0;
}

INIT_FUNC(mod_proxy_core_init) {
	plugin_data *p;

	p = calloc(1, sizeof(*p));

	/* create some backends as long as we don't have the config-parser */

	p->possible_balancers = array_init();
	array_insert_int(p->possible_balancers, "fair", PROXY_BALANCE_FAIR);
	array_insert_int(p->possible_balancers, "hash", PROXY_BALANCE_HASH);
	array_insert_int(p->possible_balancers, "round-robin", PROXY_BALANCE_RR);

	p->possible_protocols = array_init();
	array_insert_int(p->possible_protocols, "http", PROXY_PROTOCOL_HTTP);
	array_insert_int(p->possible_protocols, "fastcgi", PROXY_PROTOCOL_FASTCGI);
	array_insert_int(p->possible_protocols, "scgi", PROXY_PROTOCOL_SCGI);
	array_insert_int(p->possible_protocols, "https", PROXY_PROTOCOL_HTTPS);

	p->balance_buf = buffer_init();
	p->protocol_buf = buffer_init();
	p->replace_buf = buffer_init();
	p->backends_arr = array_init();

	p->tmp_buf = buffer_init();

	p->resp = http_response_init();

	return p;
}

FREE_FUNC(mod_proxy_core_free) {
	plugin_data *p = p_d;

	if (!p) return HANDLER_GO_ON;

	if (p->config_storage) {
		size_t i;
		for (i = 0; i < srv->config_context->used; i++) {
			plugin_config *s = p->config_storage[i];

			if (!s) continue;

			proxy_backends_free(s->backends);
			proxy_backlog_free(s->backlog);

			proxy_rewrites_free(s->request_rewrites);
			proxy_rewrites_free(s->response_rewrites);

			free(s);
		}
		free(p->config_storage);
	}

	array_free(p->possible_protocols);
	array_free(p->possible_balancers);
	array_free(p->backends_arr);

	buffer_free(p->balance_buf);
	buffer_free(p->protocol_buf);
	buffer_free(p->replace_buf);
	buffer_free(p->tmp_buf);
	
	http_response_free(p->resp);

	free(p);

	return HANDLER_GO_ON;
}

static handler_t mod_proxy_core_config_parse_rewrites(proxy_rewrites *dest, array *src, const char *config_key) {
	data_unset *du;
	size_t j;

	if (NULL != (du = array_get_element(src, config_key))) {
		data_array *keys = (data_array *)du;

		if (keys->type != TYPE_ARRAY) {
			ERROR("%s = <...>", 
				config_key);

			return HANDLER_ERROR;
		}

		/*
		 * proxy-core.rewrite-request = (
		 *   "_uri" => ( ... ) 
		 * )
		 */

		for (j = 0; j < keys->value->used; j++) {
			size_t k;
			data_array *headers = (data_array *)keys->value->data[j];

			/* keys->key should be "_uri" and the value a array of rewrite */
			if (headers->type != TYPE_ARRAY) {
				ERROR("%s = ( %s => <...> ) has to a array", 
					config_key,
					BUF_STR(headers->key));

				return HANDLER_ERROR;
			}

			if (headers->value->used > 1) {
				ERROR("%s = ( %s => <...> ) has to a array with only one element", 
					config_key,
					BUF_STR(headers->key));

				return HANDLER_ERROR;

			}

			for (k = 0; k < headers->value->used; k++) {
				data_string *rewrites = (data_string *)headers->value->data[k];
				proxy_rewrite *rw;

				/* keys->key should be "_uri" and the value a array of rewrite */
				if (rewrites->type != TYPE_STRING) {
					ERROR("%s = ( \"%s\" => ( \"%s\" => <value> ) ) has to a string", 
						config_key,
						BUF_STR(headers->key),
						BUF_STR(rewrites->key));

					return HANDLER_ERROR;
				}
			
				rw = proxy_rewrite_init();

				if (0 != proxy_rewrite_set_regex(rw, rewrites->key)) {
					return HANDLER_ERROR;
				}
				buffer_copy_string_buffer(rw->replace, rewrites->value);
				buffer_copy_string_buffer(rw->match, rewrites->key);
				buffer_copy_string_buffer(rw->header, headers->key);

				proxy_rewrites_add(dest, rw);
			}
		}
	}

	return HANDLER_GO_ON;
}


SETDEFAULTS_FUNC(mod_proxy_core_set_defaults) {
	plugin_data *p = p_d;
	size_t i, j;

	config_values_t cv[] = {
		{ CONFIG_PROXY_CORE_BACKENDS,       NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },       /* 0 */
		{ CONFIG_PROXY_CORE_DEBUG,          NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },       /* 1 */
		{ CONFIG_PROXY_CORE_BALANCER,       NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },      /* 2 */
		{ CONFIG_PROXY_CORE_PROTOCOL,       NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },      /* 3 */
		{ CONFIG_PROXY_CORE_REWRITE_REQUEST, NULL, T_CONFIG_LOCAL, T_CONFIG_SCOPE_CONNECTION },      /* 4 */
		{ CONFIG_PROXY_CORE_REWRITE_RESPONSE, NULL, T_CONFIG_LOCAL, T_CONFIG_SCOPE_CONNECTION },     /* 5 */
		{ CONFIG_PROXY_CORE_ALLOW_X_SENDFILE, NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },   /* 6 */
		{ CONFIG_PROXY_CORE_ALLOW_X_REWRITE, NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },    /* 7 */
		{ CONFIG_PROXY_CORE_MAX_POOL_SIZE, NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },        /* 8 */
		{ NULL,                        NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};

	p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));

	for (i = 0; i < srv->config_context->used; i++) {
		plugin_config *s;
		array *ca;
		proxy_backend *backend;

		array_reset(p->backends_arr);
		buffer_reset(p->balance_buf);
		buffer_reset(p->protocol_buf);

		s = calloc(1, sizeof(plugin_config));
		s->debug     = 0;
		s->balancer  = PROXY_BALANCE_UNSET;
		s->protocol  = PROXY_PROTOCOL_UNSET;
		s->backends  = proxy_backends_init();
		s->backlog   = proxy_backlog_init();
		s->response_rewrites   = proxy_rewrites_init();
		s->request_rewrites   = proxy_rewrites_init();

		cv[0].destination = p->backends_arr;
		cv[1].destination = &(s->debug);
		cv[2].destination = p->balance_buf;         /* parse into a constant */
		cv[3].destination = p->protocol_buf;        /* parse into a constant */
		cv[6].destination = &(s->allow_x_sendfile);
		cv[7].destination = &(s->allow_x_rewrite);
		cv[8].destination = &(s->max_pool_size);

		buffer_reset(p->balance_buf);

		p->config_storage[i] = s;
		ca = ((data_config *)srv->config_context->data[i])->value;

		if (0 != config_insert_values_global(srv, ca, cv)) {
			return HANDLER_ERROR;
		}

		if (!buffer_is_empty(p->balance_buf)) {
			data_integer *di;
			
			if (NULL == (di = (data_integer *)array_get_element(p->possible_balancers, BUF_STR(p->balance_buf)))) {
				ERROR("proxy.balance has to be on of 'fair', 'round-robin', 'hash', got %s", BUF_STR(p->balance_buf));

				return HANDLER_ERROR;
			}

			s->balancer = di->value;
		}

		if (!buffer_is_empty(p->protocol_buf)) {
			data_integer *di;
			
			if (NULL == (di = (data_integer *)array_get_element(p->possible_protocols, BUF_STR(p->protocol_buf)))) {
				ERROR("proxy.balance has to be on of 'fair', 'round-robin', 'hash', got %s", BUF_STR(p->protocol_buf));

				return HANDLER_ERROR;
			}

			s->protocol = di->value;
		}

		if (p->backends_arr->used) {
			backend = proxy_backend_init();

			/* check if the backends have a valid host-name */
			for (j = 0; j < p->backends_arr->used; j++) {
				data_string *ds = (data_string *)p->backends_arr->data[j];

				/* the values should be ips or hostnames */
				if (0 != proxy_address_pool_add_string(backend->address_pool, ds->value)) {
					return HANDLER_ERROR;
				}
			}

			if (s->max_pool_size) {
				backend->pool->max_size = s->max_pool_size;
			}

			proxy_backends_add(s->backends, backend);
		}

		if (HANDLER_GO_ON != mod_proxy_core_config_parse_rewrites(s->request_rewrites, ca, CONFIG_PROXY_CORE_REWRITE_REQUEST)) {
			return HANDLER_ERROR;
		}
		
		if (HANDLER_GO_ON != mod_proxy_core_config_parse_rewrites(s->response_rewrites, ca, CONFIG_PROXY_CORE_REWRITE_RESPONSE)) {
			return HANDLER_ERROR;
		}
	}

	return HANDLER_GO_ON;
}


proxy_session *proxy_session_init(void) {
	proxy_session *sess;

	sess = calloc(1, sizeof(*sess));

	sess->state = PROXY_STATE_UNSET;
	sess->request_headers = array_init();
	sess->env_headers = array_init();

	sess->recv = chunkqueue_init();
	sess->recv_raw = chunkqueue_init();
	sess->send_raw = chunkqueue_init();
	sess->send = chunkqueue_init();

	sess->is_chunked = 0;
	sess->send_response_content = 1;

	return sess;
}

void proxy_session_free(proxy_session *sess) {
	if (!sess) return;

	array_free(sess->request_headers);
	array_free(sess->env_headers);

	chunkqueue_free(sess->recv);
	chunkqueue_free(sess->recv_raw);
	chunkqueue_free(sess->send_raw);
	chunkqueue_free(sess->send);

	free(sess);
}

/**
 * decode the content for the protocol
 *
 * http might have chunk-encoding 
 * fastcgi has the fastcgi wrapper code
 *
 * @param in chunkqueue for the encoded, protocol specific data
 * @param out chunkqueue for the plain content
 */

int proxy_stream_decoder(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out) {
	switch (sess->proxy_backend->protocol) {
	case PROXY_PROTOCOL_HTTP:
		return proxy_http_stream_decoder(srv, sess, in, out);
	case PROXY_PROTOCOL_FASTCGI:
		return proxy_fastcgi_stream_decoder(srv, sess, in, out);
	default:
		ERROR("protocol %d is not supported yet", sess->proxy_backend->protocol);
		return -1;
	}
}
/**
 * encode the content for the protocol
 *
 * @param in chunkqueue with the content to (no encoding)
 * @param out chunkqueue for the encoded, protocol specific data
 */
int proxy_stream_encoder(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out) {
	switch (sess->proxy_backend->protocol) {
	case PROXY_PROTOCOL_HTTP:
		return proxy_http_stream_encoder(srv, sess, in, out);
	case PROXY_PROTOCOL_FASTCGI:
		return proxy_fastcgi_stream_encoder(srv, sess, in, out);
	default:
		ERROR("protocol %d is not supported yet", sess->proxy_backend->protocol);
		return -1;
	}
}

int proxy_get_request_chunk(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq) {
	switch (sess->proxy_backend->protocol) {
	case PROXY_PROTOCOL_HTTP:
		return proxy_http_get_request_chunk(srv, con, p, sess, cq);
	case PROXY_PROTOCOL_FASTCGI:
		return proxy_fastcgi_get_request_chunk(srv, con, p, sess, cq);
	default:
		ERROR("protocol %d is not supported yet", sess->proxy_backend->protocol);
		return -1;
	}
}


parse_status_t proxy_parse_response_header(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq) {
	switch (sess->proxy_backend->protocol) {
	case PROXY_PROTOCOL_HTTP:
		return proxy_http_parse_response_header(srv, con, p, sess, cq);
	case PROXY_PROTOCOL_FASTCGI:
		return proxy_fastcgi_parse_response_header(srv, con, p, sess, cq);
	default:
		ERROR("protocol %d is not supported yet", sess->proxy_backend->protocol);
		return PARSE_ERROR;
	}
}


handler_t proxy_connection_connect(proxy_connection *con) {
	int fd;

	if (-1 == (fd = socket(con->address->addr.plain.sa_family, SOCK_STREAM, 0))) {
	}

	fcntl(fd, F_SETFL, O_NONBLOCK | O_RDWR);

	con->sock->fd = fd;
	con->sock->fde_ndx = -1;
	con->sock->type = IOSOCKET_TYPE_SOCKET;

	if (-1 == connect(fd, &(con->address->addr.plain), sizeof(con->address->addr))) {
		switch(errno) {
		case EINPROGRESS:
		case EALREADY:
		case EINTR:
			return HANDLER_WAIT_FOR_EVENT;
		default:
			close(fd);
			con->sock->fd = -1;

			return HANDLER_ERROR;
		}
	}

	return HANDLER_GO_ON;
}

/**
 * event-handler for idling connections
 *
 * unused (idling) keep-alive connections are not bound to a session
 * and need their own event-handler 
 *
 * if the connection closes (we get a FDEVENT_IN), close our side too and 
 * let the trigger-func handle the cleanup
 *
 * @see proxy_trigger
 */


static handler_t proxy_handle_fdevent_idle(void *s, void *ctx, int revents) {
	server      *srv  = (server *)s;
	proxy_connection *proxy_con = ctx;

	if (revents & FDEVENT_IN) {
		switch (proxy_con->state) {
		case PROXY_CONNECTION_STATE_IDLE:
			proxy_con->state = PROXY_CONNECTION_STATE_CLOSED;

			/* close + unregister have to be in the same call,
			 * otherwise we get a events for a re-opened fd */

			fdevent_event_del(srv->ev, proxy_con->sock);

			break;
		case PROXY_CONNECTION_STATE_CLOSED:
			/* poll() is state-driven, we will get events as long as it isn't disabled
			 * the close() above should disable the events too */
			ERROR("%s", "hurry up buddy, I got another event for a closed idle-connection");
			break;
		default:
			ERROR("invalid connection state: %d, should be idle", proxy_con->state);
			break;
		}
	}

	return HANDLER_GO_ON;
}


/* don't call any proxy functions directly */
static handler_t proxy_handle_fdevent(void *s, void *ctx, int revents) {
	server      *srv  = (server *)s;
	proxy_session *sess = ctx;
	connection  *con  = sess->remote_con;

	if (revents & FDEVENT_OUT) {
		switch (sess->state) {
		case PROXY_STATE_CONNECTING: /* delayed connect */
		case PROXY_STATE_WRITE_REQUEST_HEADER:
		case PROXY_STATE_WRITE_REQUEST_BODY:
			/* we are still connection */

			joblist_append(srv, con);
			break;
		default:
			ERROR("oops, unexpected state for fdevent-out %d", sess->state);
			break;
		}
	} else if (revents & FDEVENT_IN) {
		chunk *c;

		switch (sess->state) {
		case PROXY_STATE_READ_RESPONSE_HEADER:
			/* call our header parser */
			joblist_append(srv, con);
			break;
		case PROXY_STATE_READ_RESPONSE_BODY:
			/* we should be in the WRITE state now, 
			 * just read in the content and forward it to the outgoing connection
			 * */

			chunkqueue_remove_finished_chunks(sess->recv_raw);
			switch (srv->network_backend_read(srv, con, sess->proxy_con->sock, sess->recv_raw)) {
			case NETWORK_STATUS_CONNECTION_CLOSE:
				fdevent_event_del(srv->ev,sess->proxy_con->sock);

				/* the connection is gone
				 * make the connect */
				con->send->is_closed = 1;
				sess->proxy_con->state = PROXY_CONNECTION_STATE_CLOSED;

			case NETWORK_STATUS_SUCCESS:
				/* read even more, do we have all the content */

				/* how much do we want to read ? */
				
				/* call stream-decoder (HTTP-chunked, FastCGI, ... ) */

				switch (proxy_stream_decoder(srv, sess, sess->recv_raw, sess->recv)) {
				case 0:
					/* need more */
					break;
				case -1:
					/* error */
					break;
				case 1:
					/* we are done */
					con->send->is_closed = 1;

					break;
				}
				chunkqueue_remove_finished_chunks(sess->recv_raw);


				/* copy the content to the next cq */
				for (c = sess->recv->first; c; c = c->next) {
					if (c->mem->used == 0) continue;

					if (sess->send_response_content) {
						/* X-Sendfile ignores the content-body */
						chunkqueue_append_mem(con->send, c->mem->ptr + c->offset, c->mem->used - c->offset);
					}
	
					c->offset = c->mem->used - 1;

				}
				chunkqueue_remove_finished_chunks(sess->recv);
				
				break;
			default:
				ERROR("%s", "oops, we failed to read");
				break;
			}

			/* we wrote something into the the send-buffers, 
			 * call the connection-handler to push it to the client */
			joblist_append(srv, con);
			break;
		default:
			ERROR("oops, unexpected state for fdevent-in %d", sess->state);
			break;
		}
	}

	if (revents & FDEVENT_HUP) {
		/* someone closed our connection */
		switch (sess->state) {
		case PROXY_STATE_CONNECTING:
			/* let the getsockopt() catch this */
			joblist_append(srv, con);
			break;
		case PROXY_STATE_READ_RESPONSE_BODY:
			/* the keep-alive race-condition */
			break;
		default:
			ERROR("oops, unexpected state for fdevent-hup %d", sess->state);
			break;
		}
	}

	return HANDLER_GO_ON;
}
void proxy_set_header(array *hdrs, const char *key, size_t key_len, const char *value, size_t val_len) {
	data_string *ds_dst;

	if (NULL != (ds_dst = (data_string *)array_get_element(hdrs, key))) {
		buffer_copy_string_len(ds_dst->value, value, val_len);
		return;
	}

	if (NULL == (ds_dst = (data_string *)array_get_unused_element(hdrs, TYPE_STRING))) {
		ds_dst = data_string_init();
	}

	buffer_copy_string_len(ds_dst->key, key, key_len);
	buffer_copy_string_len(ds_dst->value, value, val_len);
	array_insert_unique(hdrs, (data_unset *)ds_dst);
}

void proxy_append_header(array *hdrs, const char *key, size_t key_len, const char *value, size_t val_len) {
	data_string *ds_dst;

	if (NULL == (ds_dst = (data_string *)array_get_unused_element(hdrs, TYPE_STRING))) {
		ds_dst = data_string_init();
	}

	buffer_copy_string_len(ds_dst->key, key, key_len);
	buffer_copy_string_len(ds_dst->value, value, val_len);
	array_insert_unique(hdrs, (data_unset *)ds_dst);
}


/**
 * build the request-header array and call the backend specific request formater
 * to fill the chunkqueue
 */
int proxy_get_request_header(server *srv, connection *con, plugin_data *p, proxy_session *sess) {
	/* request line */
	const char *remote_ip;
	size_t i;

	remote_ip = inet_ntop_cache_get_ip(srv, &(con->dst_addr));
	proxy_append_header(sess->request_headers, CONST_STR_LEN("X-Forwarded-For"), remote_ip, strlen(remote_ip));

	/* http_host is NOT is just a pointer to a buffer
	 * which is NULL if it is not set */
	if (con->request.http_host &&
	    !buffer_is_empty(con->request.http_host)) {
		proxy_set_header(sess->request_headers, CONST_STR_LEN("X-Host"), CONST_BUF_LEN(con->request.http_host));
	}
	if (con->conf.is_ssl) {
		proxy_set_header(sess->request_headers, CONST_STR_LEN("X-Forwarded-Proto"), CONST_STR_LEN("https"));
	} else {
		proxy_set_header(sess->request_headers, CONST_STR_LEN("X-Forwarded-Proto"), CONST_STR_LEN("http"));
	}

	/* request header */
	for (i = 0; i < con->request.headers->used; i++) {
		data_string *ds;
		size_t k;

		ds = (data_string *)con->request.headers->data[i];

		if (buffer_is_empty(ds->value) || buffer_is_empty(ds->key)) continue;

		if (buffer_is_equal_string(ds->key, CONST_STR_LEN("Connection"))) continue;
		if (buffer_is_equal_string(ds->key, CONST_STR_LEN("Keep-Alive"))) continue;

		for (k = 0; k < p->conf.request_rewrites->used; k++) {
			proxy_rewrite *rw = p->conf.request_rewrites->ptr[k];

			if (buffer_is_equal(rw->header, ds->key)) {
				int ret;

				if ((ret = pcre_replace(rw->regex, rw->replace, ds->value, p->replace_buf)) < 0) {
					switch (ret) {
					case PCRE_ERROR_NOMATCH:
						/* hmm, ok. no problem */
						proxy_set_header(sess->request_headers, CONST_BUF_LEN(ds->key), CONST_BUF_LEN(ds->value));
						break;
					default:
						TRACE("oops, pcre_replace failed with: %d", ret);
						break;
					}
				} else {
					proxy_set_header(sess->request_headers, CONST_BUF_LEN(ds->key), CONST_BUF_LEN(p->replace_buf));
				}

				break;
			}
		}

		if (k == p->conf.request_rewrites->used) {
			proxy_set_header(sess->request_headers, CONST_BUF_LEN(ds->key), CONST_BUF_LEN(ds->value));
		}
	}

	proxy_get_request_chunk(srv, con, p, sess, sess->send_raw);

	return 0;
}


/* we are event-driven
 * 
 * the first entry is connect() call, if the doesn't need a event 
 *
 * a bit boring
 * - connect (+ delayed connect)
 * - write header + content
 * - read header + content
 *
 * as soon as have read the response header we switch con->file_started and return HANDLER_GO_ON to
 * tell the core we are ready to stream out the content.
 *  */
handler_t proxy_state_engine(server *srv, connection *con, plugin_data *p, proxy_session *sess) {
	/* do we have a connection ? */

	if (sess->state == PROXY_STATE_UNSET) {
		/* we are not started yet */
		sess->connect_start_ts = srv->cur_ts;
		switch(proxy_connection_connect(sess->proxy_con)) {
		case HANDLER_WAIT_FOR_EVENT:
			/* waiting on the connect call */

			fdevent_register(srv->ev, sess->proxy_con->sock, proxy_handle_fdevent, sess);
			fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_OUT);

			sess->state = PROXY_STATE_CONNECTING;
			sess->proxy_con->state = PROXY_CONNECTION_STATE_CONNECTING;
			
			return HANDLER_WAIT_FOR_EVENT;
		case HANDLER_GO_ON:
			/* we are connected */
			sess->state = PROXY_STATE_CONNECTED;
			sess->proxy_con->state = PROXY_CONNECTION_STATE_CONNECTED;
			fdevent_register(srv->ev, sess->proxy_con->sock, proxy_handle_fdevent, sess);

			break;
		case HANDLER_ERROR:
		default:
			/* not good, something failed */
			return HANDLER_ERROR;
		
		}
	} else if (sess->state == PROXY_STATE_CONNECTING) {
		int socket_error;
		socklen_t socket_error_len = sizeof(socket_error);

		fdevent_event_del(srv->ev, sess->proxy_con->sock);

		if (0 != getsockopt(sess->proxy_con->sock->fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len)) {
			ERROR("getsockopt failed:", strerror(errno));

			return HANDLER_ERROR;
		}
		if (socket_error != 0) {
			switch (socket_error) {
			case ECONNREFUSED:
				/* there is no-one on the other side */
				sess->proxy_con->address->disabled_until = srv->cur_ts + 2;

				TRACE("address %s refused us, disabling for 2 sec", sess->proxy_con->address->name->ptr);
				break;
			case EHOSTUNREACH:
				/* there is no-one on the other side */
				sess->proxy_con->address->disabled_until = srv->cur_ts + 60;

				TRACE("host %s is unreachable, disabling for 60 sec", sess->proxy_con->address->name->ptr);
				break;
			default:
				sess->proxy_con->address->disabled_until = srv->cur_ts + 60;

				TRACE("connected finally failed: %s (%d)", strerror(socket_error), socket_error);

				TRACE("connect to address %s failed and I don't know why, disabling for 10 sec", sess->proxy_con->address->name->ptr);

				break;
			}

			sess->proxy_con->address->state = PROXY_ADDRESS_STATE_DISABLED;

			sess->proxy_con->state = PROXY_CONNECTION_STATE_CLOSED;
			return HANDLER_COMEBACK;
		}

		sess->state = PROXY_STATE_CONNECTED;
		sess->proxy_con->state = PROXY_CONNECTION_STATE_CONNECTED;
	}

	if (sess->state == PROXY_STATE_CONNECTED) {
		/* build the header */
		proxy_get_request_header(srv, con, p, sess);

		sess->state = PROXY_STATE_WRITE_REQUEST_HEADER;
	}

	switch (sess->state) {
	case PROXY_STATE_WRITE_REQUEST_HEADER:
		/* create the request-packet */	
		fdevent_event_del(srv->ev, sess->proxy_con->sock);

		switch (srv->network_backend_write(srv, con, sess->proxy_con->sock, sess->send_raw)) {
		case NETWORK_STATUS_SUCCESS:
			sess->state = PROXY_STATE_WRITE_REQUEST_BODY;
			break;
		case NETWORK_STATUS_WAIT_FOR_EVENT:
			fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_OUT);

			return HANDLER_WAIT_FOR_EVENT;
		case NETWORK_STATUS_CONNECTION_CLOSE:
			sess->proxy_con->state = PROXY_CONNECTION_STATE_CLOSED;

			/* this connection is closed, restart the request with a new connection */

			return HANDLER_COMEBACK;
		default:
			return HANDLER_ERROR;
		}

		chunkqueue_remove_finished_chunks(sess->send_raw);
		
		/* fall through */
	case PROXY_STATE_WRITE_REQUEST_BODY:
		/* do we have a content-body to send up to the backend ? */

		fdevent_event_del(srv->ev, sess->proxy_con->sock);

		proxy_stream_encoder(srv, sess, con->recv, sess->send_raw);

		chunkqueue_remove_finished_chunks(con->recv);

		switch (srv->network_backend_write(srv, con, sess->proxy_con->sock, sess->send_raw)) {
		case NETWORK_STATUS_SUCCESS:
			if (con->recv->is_closed && /* no further input */
			    con->recv->bytes_in == con->recv->bytes_out &&  /* everything is encoded */
			    sess->send_raw->bytes_in == sess->send_raw->bytes_out) { /* everything is sent */
				sess->state = PROXY_STATE_READ_RESPONSE_HEADER;
			}
			break;
		case NETWORK_STATUS_WAIT_FOR_EVENT:
			fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_OUT);

			chunkqueue_remove_finished_chunks(sess->send_raw);

			return HANDLER_WAIT_FOR_EVENT;
		case NETWORK_STATUS_CONNECTION_CLOSE:
			/* the connection got close while sending the request content up 
			 * to the backend, for now handle this as error */

			TRACE("%s", "(con-close)");
			return HANDLER_ERROR;
		default:
			TRACE("%s", "(error)");
			return HANDLER_ERROR;
		}
		chunkqueue_remove_finished_chunks(sess->send_raw);

		/* fall through */
	case PROXY_STATE_READ_RESPONSE_HEADER:
		fdevent_event_del(srv->ev, sess->proxy_con->sock);

		chunkqueue_remove_finished_chunks(sess->recv_raw);

		switch (srv->network_backend_read(srv, con, sess->proxy_con->sock, sess->recv_raw)) {
		case NETWORK_STATUS_SUCCESS:
			/* we read everything from the socket, do we have a full header ? */

			switch (proxy_parse_response_header(srv, con, p, sess, sess->recv_raw)) {
			case PARSE_ERROR:
				con->http_status = 502; /* bad gateway */

				return HANDLER_FINISHED;
			case PARSE_NEED_MORE:
				/* we need more */
				fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_IN);

				return HANDLER_WAIT_FOR_EVENT;
			case PARSE_SUCCESS:
				break;
			default:
				return HANDLER_ERROR;
			}

			if (sess->do_internal_redirect) {
				/* now it becomes tricky
				 * 
				 * mod_staticfile should handle this file for us
				 * con->mode = DIRECT is taking us out of the loop */

				return HANDLER_COMEBACK;
			}

			con->file_started = 1;
			/* if Status: ... is not set, 200 is our default status-code */
			if (con->http_status == 0) con->http_status = 200;

			sess->state = PROXY_STATE_READ_RESPONSE_BODY;

			/**
			 * set the event to pass the content through to the server
			 *
			 * this triggers the event-handler
			 * @see proxy_handle_fdevent
			 */
			fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_IN);

			return HANDLER_GO_ON; /* tell http_response_prepare that we are done with the header */
		case NETWORK_STATUS_WAIT_FOR_EVENT:
			fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_IN);
			return HANDLER_WAIT_FOR_EVENT;
		case NETWORK_STATUS_CONNECTION_CLOSE:
			if (chunkqueue_length(sess->recv_raw) == 0) {
				/* the connection went away before we got something back */
				sess->proxy_con->state = PROXY_CONNECTION_STATE_CLOSED;

				/**
				 * we might run into a 'race-condition' 
				 *
				 * 1. proxy-con is keep-alive, idling and just being closed (FDEVENT_IN) [fd=27]
				 * 2. new connection comes in, we use the idling connection [fd=14]
				 * 3. we write(), successful [to fd=27]
				 * 3. we read() ... and finally receive the close-event for the connection
				 */

				ERROR("%s", "++ oops, connection closed while waiting to read a response, restarting");

				return HANDLER_COMEBACK;
			}

			ERROR("%s", "conn-close after header-read");
				
			break;
		default:
			ERROR("++ %s", "oops, something went wrong while reading");
			return HANDLER_ERROR;
		}
	case PROXY_STATE_READ_RESPONSE_BODY:
		/* if we do everything right, we won't get call for this state-anymore */

		ERROR("%s", "PROXY_STATE_READ_RESPONSE_BODY");
		
		break;
	}

	return HANDLER_GO_ON;
}

proxy_backend *proxy_get_backend(server *srv, connection *con, plugin_data *p) {
	size_t i;

	for (i = 0; i < p->conf.backends->used; i++) {
		proxy_backend *backend = p->conf.backends->ptr[i];

		return backend;
	}

	return NULL;
}

/**
 * choose a available address from the address-pool
 *
 * the backend has different balancers 
 */
proxy_address *proxy_backend_balance(server *srv, connection *con, proxy_backend *backend) {
	size_t i;
	proxy_address_pool *address_pool = backend->address_pool;
	unsigned long last_max; /* for the HASH balancer */
	proxy_address *address = NULL, *cur_address = NULL;
	int active_addresses = 0, rand_ndx;

	switch(backend->balancer) {
	case PROXY_BALANCE_HASH:
		/* hash balancing */

		for (i = 0, last_max = ULONG_MAX; i < address_pool->used; i++) {
			unsigned long cur_max;

			cur_address = address_pool->ptr[i];

			if (cur_address->state != PROXY_ADDRESS_STATE_ACTIVE) continue;

			cur_max = generate_crc32c(CONST_BUF_LEN(con->uri.path)) +
				generate_crc32c(CONST_BUF_LEN(cur_address->name)) + /* we can cache this */
				generate_crc32c(CONST_BUF_LEN(con->uri.authority));
#if 0
			TRACE("hash-election: %s - %s - %s: %ld", 
					con->uri.path->ptr,
					cur_address->name->ptr,
					con->uri.authority->ptr,
					cur_max);
#endif
			if (address == NULL || (cur_max > last_max)) {
				last_max = cur_max;

				address = cur_address;
			}
		}

		break;
	case PROXY_BALANCE_FAIR:
		/* fair balancing */

		for (i = 0; i < address_pool->used; i++) {
			cur_address = address_pool->ptr[i];

			if (cur_address->state != PROXY_ADDRESS_STATE_ACTIVE) continue;

			/* the address is up, use it */

			address = cur_address;

			break;
		}

		break;
	case PROXY_BALANCE_RR:
		/* round robin */

		/**
		 * instead of real RoundRobin we just do a RandomSelect
		 *
		 * it is state-less and has the same distribution
		 */

		active_addresses = 0;
		
		for (i = 0; i < address_pool->used; i++) {
			cur_address = address_pool->ptr[i];

			if (cur_address->state != PROXY_ADDRESS_STATE_ACTIVE) continue;

			active_addresses++;
		}

		rand_ndx = (int) (1.0 * active_addresses * rand()/(RAND_MAX));
	
		active_addresses = 0;
		for (i = 0; i < address_pool->used; i++) {
			cur_address = address_pool->ptr[i];

			if (cur_address->state != PROXY_ADDRESS_STATE_ACTIVE) continue;

			address = cur_address;

			if (rand_ndx == active_addresses++) break;
		}

		break;
	default:
		break;
	}

	return address;
}

static int mod_proxy_core_patch_connection(server *srv, connection *con, plugin_data *p) {
	size_t i, j;
	plugin_config *s = p->config_storage[0];

	/* global defaults */
	PATCH_OPTION(balancer);
	PATCH_OPTION(debug);
	PATCH_OPTION(backends);
	PATCH_OPTION(backlog);
	PATCH_OPTION(protocol);
	PATCH_OPTION(request_rewrites);
	PATCH_OPTION(response_rewrites);
	PATCH_OPTION(allow_x_sendfile);
	PATCH_OPTION(allow_x_rewrite);

	/* skip the first, the global context */
	for (i = 1; i < srv->config_context->used; i++) {
		data_config *dc = (data_config *)srv->config_context->data[i];
		s = p->config_storage[i];

		/* condition didn't match */
		if (!config_check_cond(srv, con, dc)) continue;

		/* merge config */
		for (j = 0; j < dc->value->used; j++) {
			data_unset *du = dc->value->data[j];

			if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_BACKENDS))) {
				PATCH_OPTION(backends);
				PATCH_OPTION(backlog);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_DEBUG))) {
				PATCH_OPTION(debug);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_BALANCER))) {
				PATCH_OPTION(balancer);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_PROTOCOL))) {
				PATCH_OPTION(protocol);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_REWRITE_REQUEST))) {
				PATCH_OPTION(request_rewrites);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_REWRITE_RESPONSE))) {
				PATCH_OPTION(response_rewrites);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_ALLOW_X_SENDFILE))) {
				PATCH_OPTION(allow_x_sendfile);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN(CONFIG_PROXY_CORE_ALLOW_X_REWRITE))) {
				PATCH_OPTION(allow_x_rewrite);
			}
		}
	}

	return 0;
}

SUBREQUEST_FUNC(mod_proxy_core_check_extension) {
	plugin_data *p = p_d;
	proxy_session *sess = con->plugin_ctx[p->id]; /* if this is the second round, sess is already prepared */

	/* check if we have a matching conditional for this request */

	if (buffer_is_empty(con->uri.path)) return HANDLER_GO_ON;

	mod_proxy_core_patch_connection(srv, con, p);

	if (p->conf.backends->used == 0) return HANDLER_GO_ON;

	if (!sess) {
		/* a session lives for a single request */
		sess = proxy_session_init();

		con->plugin_ctx[p->id] = sess;
		con->mode = p->id;

		sess->remote_con = con;
	}

	return HANDLER_GO_ON;
}

CONNECTION_FUNC(mod_proxy_core_start_backend) {
	plugin_data *p = p_d;
	proxy_session *sess = con->plugin_ctx[p->id];

	if (p->id != con->mode) return HANDLER_GO_ON;

	/* 
	 * 0. build session
	 * 1. get a proxy connection
	 * 2. create the http-request header
	 * 3. stream the content to the backend 
	 * 4. wait for http-response header 
	 * 5. decode the response + parse the response
	 * 6. stream the response-content to the client 
	 * 7. kill session
	 * */


	assert(sess);

	if (sess->do_internal_redirect) {
	       if (sess->internal_redirect_count > MAX_INTERNAL_REDIRECTS) {
			/* we already handled this request and sent it to the static file handling */

			return HANDLER_GO_ON;
		}
	}

	switch (sess->state) {
	case PROXY_STATE_CONNECTING:
		/* this connections is waited 10 seconds to connect to the backend
		 * and didn't got a successful connection yet, sending timeout */
		if (srv->cur_ts - sess->connect_start_ts > 10) {
			con->http_status = 504; /* gateway timeout */
			con->send->is_closed = 1;

			if (sess->proxy_con) {
				/* if we are waiting for a proxy-connection right now, close it */
				proxy_connection_pool_remove_connection(sess->proxy_backend->pool, sess->proxy_con);
	
				fdevent_event_del(srv->ev, sess->proxy_con->sock);
				fdevent_unregister(srv->ev, sess->proxy_con->sock);

				proxy_connection_free(sess->proxy_con);
			
				sess->proxy_con = NULL;
			}

			TRACE("%s", "connect to backend timed out");
			
			return HANDLER_FINISHED;
		}
	default:
		/* handle-request-timeout,  */
#if 0
		if (srv->cur_ts - con->request_start > 60) {
			TRACE("request runs longer than 60sec: current state: %d", sess->state);
		}
#endif
		break;
	}

	/* if the WRITE fails from the start, restart the connection */
	while (1) {
		if (sess->proxy_con == NULL) {
			proxy_address *address = NULL;
			if (NULL == (sess->proxy_backend = proxy_get_backend(srv, con, p))) {
				/* no connection pool for this location */
				SEGFAULT();
			}

			sess->proxy_backend->balancer = p->conf.balancer;
			sess->proxy_backend->protocol = p->conf.protocol;

			/**
			 * ask the balancer for the next address and
			 * check the connection pool if we have a connection open
			 * for that address
			 */
			if (NULL == (address = proxy_backend_balance(srv, con, sess->proxy_backend))) {
				/* we don't have any backends to connect to */
				proxy_request *req;

				/* connection pool is full, queue the request for now */
				req = proxy_request_init();
				req->added_ts = srv->cur_ts;
				req->con = con;
				
				TRACE("backlog: all backends are down, putting %s (%d) into the backlog", BUF_STR(con->uri.path), con->sock->fd);
				proxy_backlog_push(p->conf.backlog, req);

				/* no, not really a event, 
				 * we just want to block the outer loop from stepping forward
				 *
				 * the trigger will bring this connection back into the game
				 * */
				return HANDLER_WAIT_FOR_EVENT;
			}

			if (PROXY_CONNECTIONPOOL_FULL == proxy_connection_pool_get_connection(
						sess->proxy_backend->pool, 
						address,
						&(sess->proxy_con))) {
				proxy_request *req;

				/* connection pool is full, queue the request for now */
				req = proxy_request_init();
				req->added_ts = srv->cur_ts;
				req->con = con;
			
#if 0	
				TRACE("backlog: the con-pool is full, putting %s (%d) into the backlog", BUF_STR(con->uri.path), con->sock->fd);
#endif
				proxy_backlog_push(p->conf.backlog, req);

				/* no, not really a event, 
				 * we just want to block the outer loop from stepping forward
				 *
				 * the trigger will bring this connection back into the game
				 * */
				return HANDLER_WAIT_FOR_EVENT;
			}

			/* a fresh connection, we need address for it */
			if (sess->proxy_con->state == PROXY_CONNECTION_STATE_CONNECTING) {
				sess->state = PROXY_STATE_UNSET;
				sess->bytes_read = 0;
			} else {
				/* we are already connected */
				sess->state = PROXY_STATE_CONNECTED;
				
				/* the connection was idling and using the fdevent_idle-handler 
				 * switch it back to the normal proxy-event-handler */
				fdevent_event_del(srv->ev, sess->proxy_con->sock);
				fdevent_unregister(srv->ev, sess->proxy_con->sock);

				fdevent_register(srv->ev, sess->proxy_con->sock, proxy_handle_fdevent, sess);
				fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_IN);
			}
		}

		switch (proxy_state_engine(srv, con, p, sess)) {
		case HANDLER_WAIT_FOR_EVENT:
			return HANDLER_WAIT_FOR_EVENT;
		case HANDLER_COMEBACK:
			proxy_connection_pool_remove_connection(sess->proxy_backend->pool, sess->proxy_con);
	
			fdevent_event_del(srv->ev, sess->proxy_con->sock);
			fdevent_unregister(srv->ev, sess->proxy_con->sock);

			proxy_connection_free(sess->proxy_con);

			sess->proxy_con = NULL;

			if (sess->do_internal_redirect) {
				con->mode = DIRECT;
				con->http_status = 0;

				return HANDLER_COMEBACK;
			}
			/* restart the connection to the backend */
			TRACE("%s", "write failed, restarting request");
			break;
		case HANDLER_GO_ON:
			return HANDLER_GO_ON;
		default:
			TRACE("state: %d (error)", sess->state);
			return HANDLER_ERROR;
		}
	}

	TRACE("state: %d", sess->state);
	/* should not be reached */
	return HANDLER_ERROR;
}

CONNECTION_FUNC(mod_proxy_send_request_content) {
	plugin_data *p = p_d;
	proxy_session *sess = con->plugin_ctx[p->id]; 

	if (p->id != con->mode) return HANDLER_GO_ON;

	/* read all the content before we start our backend */
	if (!con->recv->is_closed) {
		return HANDLER_GO_ON;
	}

	/* copy the chunks to our queue and call the state-engine to send it out */
	return mod_proxy_core_start_backend(srv, con, p_d);
}
/**
 * end of the connection to the client
 */
REQUESTDONE_FUNC(mod_proxy_connection_close_callback) {
	plugin_data *p = p_d;
	
	if (con->mode != p->id) return HANDLER_GO_ON;

	return HANDLER_GO_ON;
}

/**
 * end of a request
 */
CONNECTION_FUNC(mod_proxy_connection_reset) {
	plugin_data *p = p_d;
	proxy_session *sess = con->plugin_ctx[p->id]; 
	proxy_request *req;

	if (!sess) return HANDLER_GO_ON;

	if (sess->proxy_con) {
		switch (sess->proxy_con->state) {
		case PROXY_CONNECTION_STATE_CONNECTED:
			if (!sess->is_closing) {
				sess->proxy_con->state = PROXY_CONNECTION_STATE_IDLE;

				/* don't ignore events as the FD is idle
				 * we might get a HUP as the remote connection might close */
				fdevent_event_del(srv->ev, sess->proxy_con->sock);
				fdevent_unregister(srv->ev, sess->proxy_con->sock);

				fdevent_register(srv->ev, sess->proxy_con->sock, proxy_handle_fdevent_idle, sess->proxy_con);
				fdevent_event_add(srv->ev, sess->proxy_con->sock, FDEVENT_IN);

				break;
			}

			/* fall-through for non-keep-alive */

		case PROXY_CONNECTION_STATE_CLOSED:
			proxy_connection_pool_remove_connection(sess->proxy_backend->pool, sess->proxy_con);
	
			fdevent_event_del(srv->ev, sess->proxy_con->sock);
			fdevent_unregister(srv->ev, sess->proxy_con->sock);

			proxy_connection_free(sess->proxy_con);
			break;
		case PROXY_CONNECTION_STATE_IDLE:
			TRACE("%s", "... connection is already back in the pool");
			break;
		default:
			ERROR("connection is in a unexpected state at close-time: %d", sess->proxy_con->state);
			break;
		}
	} else {
		/* if we have the connection in the backlog, remove it */
		proxy_backlog_remove_connection(p->conf.backlog, con);
	}
	

	proxy_session_free(sess);

	con->plugin_ctx[p->id] = NULL;

	/* wake up a connection from the backlog */
	if ((req = proxy_backlog_shift(p->conf.backlog))) {
		connection *next_con = req->con;

		joblist_append(srv, next_con);

		proxy_request_free(req);
	}

	
	return HANDLER_GO_ON;
}



/**
 * cleanup dead connections once a second
 *
 * the idling event-handler can't cleanup connections itself and has to wait until the 
 * trigger cleans up
 */
handler_t mod_proxy_trigger_context(server *srv, plugin_config *p) {
	size_t i, j;
	proxy_request *req;

	for (i = 0; i < p->backends->used; i++) {
		proxy_backend *backend = p->backends->ptr[i];
		proxy_connection_pool *pool = backend->pool;
		proxy_address_pool *address_pool = backend->address_pool;

		for (j = 0; j < pool->used; ) {
			proxy_connection *proxy_con = pool->ptr[j];

			/* remove-con is removing the current con and moves the good connections to the left
			 * no need to increment i */
			if (proxy_con->state == PROXY_CONNECTION_STATE_CLOSED) {
				proxy_connection_pool_remove_connection(backend->pool, proxy_con);
	
				fdevent_event_del(srv->ev, proxy_con->sock);
				fdevent_unregister(srv->ev, proxy_con->sock);

				proxy_connection_free(proxy_con);
			} else {
				j++;
			}
		}

		/* active the disabled addresses again */
		for (j = 0; j < address_pool->used; j++) {
			proxy_address *address = address_pool->ptr[j];

			if (address->state != PROXY_ADDRESS_STATE_DISABLED) continue;

			if (srv->cur_ts > address->disabled_until) {
				address->disabled_until = 0;
				address->state = PROXY_ADDRESS_STATE_ACTIVE;
			}
		}
	}

	/* wake up the connections from the backlog */
	while ((req = proxy_backlog_shift(p->backlog))) {
		connection *con = req->con;

		joblist_append(srv, con);

		proxy_request_free(req);
	}
	
	return HANDLER_GO_ON;
}

TRIGGER_FUNC(mod_proxy_trigger) {
	plugin_data *p = p_d;
	size_t i;
	
	for (i = 0; i < srv->config_context->used; i++) {
		mod_proxy_trigger_context(srv, p->config_storage[i]);
	}

	return HANDLER_GO_ON;
}

int mod_proxy_core_plugin_init(plugin *p) {
	p->version      = LIGHTTPD_VERSION_ID;
	p->name         = buffer_init_string("mod_proxy_core");

	p->init         = mod_proxy_core_init;
	p->cleanup      = mod_proxy_core_free;
	p->set_defaults = mod_proxy_core_set_defaults;
	p->handle_physical         = mod_proxy_core_check_extension;
	p->handle_send_request_content = mod_proxy_send_request_content;
	p->connection_reset        = mod_proxy_connection_reset;
	p->handle_connection_close = mod_proxy_connection_close_callback;
	p->handle_trigger          = mod_proxy_trigger;

	p->data         = NULL;

	return 0;
}
