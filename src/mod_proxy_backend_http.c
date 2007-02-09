#include <stdlib.h>
#include <string.h>

#include "mod_proxy_core.h"
#include "mod_proxy_core_protocol.h"
#include "configfile.h"
#include "buffer.h"
#include "log.h"
#include "sys-strings.h"

#define CORE_PLUGIN "mod_proxy_core"

typedef struct {
	PLUGIN_DATA;

	proxy_protocol *protocol;
} protocol_plugin_data;

typedef enum {
	HTTP_CHUNK_LEN,
	HTTP_CHUNK_EXTENSION,
	HTTP_CHUNK_DATA,
	HTTP_CHUNK_END
} http_chunk_state_t;

/**
 * The protocol will use this struct for storing state variables
 * used in decoding the stream
 */
typedef struct {
	http_chunk_state_t chunk_parse_state;
	off_t chunk_len;
	off_t chunk_offset;
	buffer *buf;
} protocol_state_data;

protocol_state_data *protocol_state_data_init(void) {
	protocol_state_data *data;

	data = calloc(1, sizeof(*data));
	data->chunk_parse_state = HTTP_CHUNK_LEN;
	data->buf = buffer_init();

	return data;
}

void protocol_state_data_free(protocol_state_data *data) {
	buffer_free(data->buf);
	free(data);
}

void protocol_state_data_reset(protocol_state_data *data) {
	buffer_reset(data->buf);
	data->chunk_parse_state = HTTP_CHUNK_LEN;
}

/*
SESSION_FUNC(proxy_http_init) {
	return 1;
}
*/

SESSION_FUNC(proxy_http_cleanup) {
	UNUSED(srv);

	if(sess->protocol_data) {
		protocol_state_data_free((protocol_state_data *)sess->protocol_data);
		sess->protocol_data = NULL;
	}
	return 1;
}

int proxy_http_parse_chunked_stream(server *srv, protocol_state_data *data, chunkqueue *in,
	                           chunkqueue *out) {
	char *err = NULL;
	off_t we_have = 0, we_want = 0;
	off_t chunk_len = 0;
	size_t offset = 0;
	buffer *b;
	chunk *c;
	char ch = '\0';
	int finished = 0;

	UNUSED(srv);

	for (c = in->first; c && !finished;) {
		if(c->mem->used == 0) {
			c = c->next;
			continue;
		}
		switch(data->chunk_parse_state) {
		case HTTP_CHUNK_LEN:
			/* parse chunk len. */
			for(offset = c->offset; offset < (c->mem->used - 1) ; offset++) {
				ch = c->mem->ptr[offset];
				if(!light_isxdigit(ch)) break;
			}
			if(offset > c->offset) {
				buffer_append_string_len(data->buf, (c->mem->ptr + c->offset), offset - c->offset);
				in->bytes_out += (offset - c->offset);
				c->offset = offset;
			}
			if (!(ch == ' ' || ch == '\r' || ch == ';')) {
				if (ch == '\0') {
					/* get next chunk from queue */
					break;
				}
				/* protocol error.  bad http-chunk len */
				return -1;
			}
			data->chunk_len = strtol(BUF_STR(data->buf), &err, 16);
			data->chunk_offset = 0;
			buffer_reset(data->buf);
			data->chunk_parse_state = HTTP_CHUNK_EXTENSION;
		case HTTP_CHUNK_EXTENSION:
			/* find CRLF.  discard chunk-extension */
			for(ch = 0; c->offset < (c->mem->used - 1) && ch != '\n' ;) {
				ch = c->mem->ptr[c->offset];
				c->offset++;
				in->bytes_out++;
			}
			if(ch != '\n') {
				/* get next chunk from queue */
				break;
			}
			if(data->chunk_len > 0) {
				data->chunk_parse_state = HTTP_CHUNK_DATA;
			} else {
				data->chunk_parse_state = HTTP_CHUNK_END;
			}
		case HTTP_CHUNK_DATA:
			chunk_len = data->chunk_len - data->chunk_offset;
			/* copy chunk_len bytes from in queue to out queue. */
			we_have = c->mem->used - c->offset - 1;
			we_want = chunk_len > we_have ? we_have : chunk_len;

			if (c->offset == 0 && we_want == we_have) {
				/* we are copying the whole buffer, just steal it */
				chunkqueue_steal_chunk(out, c);
			} else {
				b = chunkqueue_get_append_buffer(out);
				buffer_copy_string_len(b, c->mem->ptr + c->offset, we_want);
				c->offset += we_want;
			}

			chunk_len -= we_want;
			out->bytes_in += we_want;
			in->bytes_out += we_want;
			data->chunk_offset += we_want;
			if(chunk_len > 0) {
				/* get next chunk from queue */
				break;
			}
			data->chunk_offset = 0;
			data->chunk_parse_state = HTTP_CHUNK_END;
		case HTTP_CHUNK_END:
			/* discard CRLF.*/
			for(ch = 0; c->offset < (c->mem->used - 1) && ch != '\n' ;) {
				ch = c->mem->ptr[c->offset];
				c->offset++;
				in->bytes_out++;
			}
			if(ch != '\n') {
				/* get next chunk from queue */
				break;
			}
			/* final chunk */
			if(data->chunk_len == 0) {
				finished = 1;
			}
			/* finished http-chunk.  reset and parse next chunk. */
			protocol_state_data_reset(data);
			break;
		}
		if(c->offset == c->mem->used - 1) {
			c = c->next;
		}
	}
	chunkqueue_remove_finished_chunks(in);
	/* ran out of data. */
	return finished;
}

STREAM_IN_OUT_FUNC(proxy_http_stream_decoder) {
	chunk *c;

	if (in->first == NULL) {
		if (in->is_closed) return 1;

		return 0;
	}

	if (sess->is_chunked) {
		int rc;
		protocol_state_data *data = (protocol_state_data *)sess->protocol_data;
		if(!data) {
			data = protocol_state_data_init();
			sess->protocol_data = data;
		}
		rc = proxy_http_parse_chunked_stream(srv, data, in, out);
		return rc;
	} else {
		/* no chunked encoding, ok, perhaps a content-length ? */

		chunkqueue_remove_finished_chunks(in);
		for (c = in->first; c; c = c->next) {
			buffer *b;

			if (c->mem->used == 0) continue;

			out->bytes_in += c->mem->used - c->offset - 1;
			in->bytes_out += c->mem->used - c->offset - 1;

			sess->bytes_read += c->mem->used - c->offset - 1;

			if (c->offset == 0) {
				/* we are copying the whole buffer, just steal it */

				chunkqueue_steal_chunk(out, c);
			} else {
				b = chunkqueue_get_append_buffer(out);
				buffer_copy_string_len(b, c->mem->ptr + c->offset, c->mem->used - c->offset - 1);
				c->offset = c->mem->used - 1; /* marks is read */
			}


			if (sess->bytes_read == sess->content_length) {
				break;
			}

		}

		if (in->is_closed || sess->bytes_read == sess->content_length) {
			return 1; /* finished */
		}
	}

	return 0;
}

/**
 * transform the content-stream into a valid HTTP-content-stream
 *
 * as we don't apply chunked-encoding here, pass it on AS IS
 */
STREAM_IN_OUT_FUNC(proxy_http_stream_encoder) {
	chunk *c;

	UNUSED(srv);
	UNUSED(sess);

	/* there is nothing that we have to send out anymore */
	if (in->bytes_in == in->bytes_out &&
	    in->is_closed) return 0;

	for (c = in->first; in->bytes_out < in->bytes_in; c = c->next) {
		buffer *b;
		off_t weWant = in->bytes_in - in->bytes_out;
		off_t weHave = 0;

		/* we announce toWrite octects
		 * now take all the request_content chunk that we need to fill this request
		 */

		switch (c->type) {
		case FILE_CHUNK:
			weHave = c->file.length - c->offset;

			if (weHave > weWant) weHave = weWant;

			/** steal the chunk from the incoming chunkqueue */
			chunkqueue_steal_tempfile(out, c);

			c->offset += weHave;
			in->bytes_out += weHave;

			out->bytes_in += weHave;

			break;
		case MEM_CHUNK:
			/* append to the buffer */
			weHave = c->mem->used - 1 - c->offset;

			if (weHave > weWant) weHave = weWant;

			b = chunkqueue_get_append_buffer(out);
			buffer_append_memory(b, c->mem->ptr + c->offset, weHave);
			b->used++; /* add virtual \0 */

			c->offset += weHave;
			in->bytes_out += weHave;

			out->bytes_in += weHave;

			break;
		default:
			break;
		}
	}

	return 0;

}
/**
 * generate a HTTP/1.1 proxy request from the set of request-headers
 *
 */
STREAM_IN_OUT_FUNC(proxy_http_get_request_chunk) {
	connection *con = sess->remote_con;
	buffer *b;
	size_t i;

	UNUSED(srv);
	UNUSED(in);

	b = chunkqueue_get_append_buffer(out);

	/* request line */
	buffer_copy_string(b, get_http_method_name(con->request.http_method));
	BUFFER_APPEND_STRING_CONST(b, " ");

	/* request uri */
	buffer_append_string_buffer(b, sess->request_uri);

	if (con->request.http_version == HTTP_VERSION_1_1) {
		BUFFER_APPEND_STRING_CONST(b, " HTTP/1.1\r\n");
	} else {
		BUFFER_APPEND_STRING_CONST(b, " HTTP/1.0\r\n");
	}

	for (i = 0; i < sess->request_headers->used; i++) {
		data_string *ds;

		ds = (data_string *)sess->request_headers->data[i];

		buffer_append_string_buffer(b, ds->key);
		BUFFER_APPEND_STRING_CONST(b, ": ");
		buffer_append_string_buffer(b, ds->value);
		BUFFER_APPEND_STRING_CONST(b, "\r\n");
	}

	BUFFER_APPEND_STRING_CONST(b, "\r\n");

	out->bytes_in += b->used - 1;

	return 0;
}

/**
 * parse the response header
 *
 * NOTE: this can be used by all backends as they all send a HTTP-Response a clean block
 * - fastcgi needs some decoding for the protocol
 */
STREAM_IN_OUT_FUNC(proxy_http_parse_response_header) {
	UNUSED(srv);
	UNUSED(out);

	http_response_reset(sess->resp);

	/* backend response already in HTTP response format, no special parsing needed. */
	return http_response_parse_cq(in, sess->resp);
}

INIT_FUNC(mod_proxy_backend_http_init) {
	mod_proxy_core_plugin_data *core_data;
	protocol_plugin_data *p;

	/* get the plugin_data of the core-plugin */
	core_data = plugin_get_config(srv, CORE_PLUGIN);
	if(!core_data) return NULL;

	p = calloc(1, sizeof(*p));

	/* define protocol handler callbacks */
	p->protocol = (core_data->proxy_register_protocol)("http");

	/* p->protocol->proxy_stream_init = proxy_http_init; */
	p->protocol->proxy_stream_cleanup = proxy_http_cleanup;
	p->protocol->proxy_stream_decoder = proxy_http_stream_decoder;
	p->protocol->proxy_stream_encoder = proxy_http_stream_encoder;
	p->protocol->proxy_get_request_chunk = proxy_http_get_request_chunk;
	p->protocol->proxy_parse_response_header = proxy_http_parse_response_header;

	return p;
}

/*
FREE_FUNC(mod_proxy_backend_http_free) {
	return HANDLER_GO_ON;
}
*/

int mod_proxy_backend_http_plugin_init(plugin *p) {
	data_string *ds;

	p->version      = LIGHTTPD_VERSION_ID;
	p->name         = buffer_init_string("mod_proxy_backend_http");

	p->init         = mod_proxy_backend_http_init;
	/*p->cleanup      = mod_proxy_backend_http_free;*/

	p->data         = NULL;

	ds = data_string_init();
	buffer_copy_string(ds->value, CORE_PLUGIN);
	array_insert_unique(p->required_plugins, (data_unset *)ds);

	return 0;
}

