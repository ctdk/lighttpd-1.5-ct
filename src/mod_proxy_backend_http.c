#include <stdlib.h>
#include <string.h>

#include "mod_proxy_core.h"
#include "configfile.h"
#include "buffer.h"
#include "log.h"
#include "sys-strings.h"

void chunkqueue_skip(chunkqueue *cq, off_t skip) {
	chunk *c;

	for (c = cq->first; c && skip; c = c->next) {
		if (skip > c->mem->used - c->offset - 1) {
			skip -= c->mem->used - c->offset - 1;
		} else {
			c->offset += skip;
			skip = 0;
		}
	}

	return;
}

int proxy_http_stream_decoder(server *srv, proxy_session *sess, chunkqueue *raw, chunkqueue *decoded) {
	chunk *c;

	if (raw->first == NULL) return 0;

	if (sess->is_chunked) {
		do {
			/* the start should always be a chunk-length */
			off_t chunk_len = 0;
			char *err = NULL;
			int chunklen_strlen = 0;
			char ch;
			off_t we_have = 0, we_need = 0;

			c = raw->first;

			if (c->mem->used == 0) return 0;

			chunk_len = strtol(BUF_STR(c->mem) + c->offset, &err, 16);
			if (!(*err == ' ' || *err == '\r' || *err == ';')) {
				if (*err == '\0') {
					/* we just need more data */
					return 0;
				}
				return -1;
			}

			if (chunk_len < 0) {
				ERROR("chunk_len is negative: %Ld", chunk_len);
				return -1;
			}

			chunklen_strlen = err - (BUF_STR(c->mem) + c->offset);
			chunklen_strlen++; /* skip the err-char */ 
			
			do {
				ch = BUF_STR(c->mem)[c->offset + chunklen_strlen];
	
				switch (ch) {
				case '\n':
				case '\0':
					/* bingo, chunk-header is finished */
					break;
				default:
					break;
				}
				chunklen_strlen++;
			} while (ch != '\n' && c != '\0');

			if (ch != '\n') {
				ERROR("%s", "missing the CRLF");
				return 0;
			}

			we_need = chunk_len + chunklen_strlen + 2;
			/* do we have the full chunk ? */
			for (c = raw->first; c; c = c->next) {
				we_have += c->mem->used - 1 - c->offset;

				/* we have enough, jump out */
				if (we_have > we_need) break;
			}

			/* get more data */
			if (we_have < we_need) {
				return 0;
			}

			/* skip the chunk-header */
			chunkqueue_skip(raw, chunklen_strlen);

			/* final chunk */
			if (chunk_len == 0) {
				chunkqueue_skip(raw, 2);

				return 1;
			}

			/* we have enough, copy the data */	
			for (c = raw->first; c && chunk_len; c = c->next) {
				off_t we_want = 0;
				buffer *b = chunkqueue_get_append_buffer(decoded);

				we_want = chunk_len > (c->mem->used - c->offset - 1) ? c->mem->used - c->offset - 1: chunk_len;

				buffer_copy_string_len(b, c->mem->ptr + c->offset, we_want);

				c->offset += we_want;
				chunk_len -= we_want;
			}

			/* skip the \r\n */
			chunkqueue_skip(raw, 2);

			/* we are done, give the connection to someone else */
			chunkqueue_remove_finished_chunks(raw);
		} while (1);
	} else {
		/* no chunked encoding, ok, perhaps a content-length ? */

		chunkqueue_remove_finished_chunks(raw);
		for (c = raw->first; c; c = c->next) {
			buffer *b;

			if (c->mem->used == 0) continue;
		       
			b = chunkqueue_get_append_buffer(decoded);

			sess->bytes_read += c->mem->used - c->offset - 1;

			buffer_copy_string_len(b, c->mem->ptr + c->offset, c->mem->used - c->offset - 1);

			c->offset = c->mem->used - 1;

			if (sess->bytes_read == sess->content_length) {
				break;
			}

		}
	    	if (sess->bytes_read == sess->content_length) {
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
int proxy_http_stream_encoder(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out) {
	chunk *c;

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

			chunkqueue_append_file(out, c->file.name, c->offset, weHave);

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
 * TODO: this is HTTP-proxy specific and will be moved moved into a separate backed
 *
 */
int proxy_http_get_request_chunk(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq) {
	buffer *b;
	size_t i;
	
	b = chunkqueue_get_append_buffer(cq);

	/* request line */
	buffer_copy_string(b, get_http_method_name(con->request.http_method));
	BUFFER_APPEND_STRING_CONST(b, " ");

	/* check if we want to rewrite the uri */

	for (i = 0; i < p->conf.request_rewrites->used; i++) {
		proxy_rewrite *rw = p->conf.request_rewrites->ptr[i];

		if (buffer_is_equal_string(rw->header, CONST_STR_LEN("_uri"))) {
			int ret;

			if ((ret = pcre_replace(rw->regex, rw->replace, con->request.uri, p->replace_buf)) < 0) {
				switch (ret) {
				case PCRE_ERROR_NOMATCH:
					/* hmm, ok. no problem */
					buffer_append_string_buffer(b, con->request.uri);
					break;
				default:
					TRACE("oops, pcre_replace failed with: %d", ret);
					break;
				}
			} else {
				buffer_append_string_buffer(b, p->replace_buf);
			}

			break;
		}
	}

	if (i == p->conf.request_rewrites->used) {
		/* not found */
		buffer_append_string_buffer(b, con->request.uri);
	}

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
	
	return 0;
}

/**
 * parse the response header
 *
 * NOTE: this can be used by all backends as they all send a HTTP-Response a clean block
 * - fastcgi needs some decoding for the protocol
 */
parse_status_t proxy_http_parse_response_header(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq) {
	int have_content_length = 0;
	size_t i;

	http_response_reset(p->resp);
	
	switch (http_response_parse_cq(cq, p->resp)) {
	case PARSE_ERROR:
		/* parsing failed */

		return PARSE_ERROR;
	case PARSE_NEED_MORE:
		return PARSE_NEED_MORE;
	case PARSE_SUCCESS:
		con->http_status = p->resp->status;

		chunkqueue_remove_finished_chunks(cq);

		sess->content_length = -1;

		/* copy the http-headers */
		for (i = 0; i < p->resp->headers->used; i++) {
			const char *ign[] = { "Status", NULL };
			size_t j, k;
			data_string *ds;

			data_string *header = (data_string *)p->resp->headers->data[i];

			/* some headers are ignored by default */
			for (j = 0; ign[j]; j++) {
				if (0 == strcasecmp(ign[j], header->key->ptr)) break;
			}
			if (ign[j]) continue;

			if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("Location"))) {
				/* CGI/1.1 rev 03 - 7.2.1.2 */
				if (con->http_status == 0) con->http_status = 302;
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("Content-Length"))) {
				have_content_length = 1;

				sess->content_length = strtol(header->value->ptr, NULL, 10);

				if (sess->content_length < 0) {
					return PARSE_ERROR;
				}
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("X-Sendfile")) ||
				   0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("X-LIGHTTPD-Sendfile"))) {
				if (p->conf.allow_x_sendfile) {
					sess->send_response_content = 0;
					sess->do_internal_redirect = 1;
					
					/* don't try to rewrite this request through mod_proxy_core again */
					sess->internal_redirect_count = MAX_INTERNAL_REDIRECTS; 

					buffer_copy_string_buffer(con->physical.path, header->value);

					/* as we want to support ETag and friends we set the physical path for the file
					 * and hope mod_staticfile catches up */
				}

				continue;
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("X-Rewrite-URI"))) { 
				if (p->conf.allow_x_rewrite) {
					sess->send_response_content = 0;
					sess->do_internal_redirect = 1;

					buffer_copy_string_buffer(con->request.uri, header->value);
					buffer_reset(con->physical.path);

					config_cond_cache_reset(srv, con);
				}

				continue;
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("X-Rewrite-Host"))) { 
				if (p->conf.allow_x_rewrite) {
					sess->send_response_content = 0;
					sess->do_internal_redirect = 1;

					buffer_copy_string_buffer(con->request.http_host, header->value);
					buffer_reset(con->physical.path);

					config_cond_cache_reset(srv, con);
				}

				continue;
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("Transfer-Encoding"))) {
				if (strstr(header->value->ptr, "chunked")) {
					sess->is_chunked = 1;
				}
				/* ignore the header */
				continue;
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("Connection"))) {
				if (strstr(header->value->ptr, "close")) {
					sess->is_closing = 1;
				}
				/* ignore the header */
				continue;

			}
			
			if (NULL == (ds = (data_string *)array_get_unused_element(con->response.headers, TYPE_STRING))) {
				ds = data_response_init();
			}


			buffer_copy_string_buffer(ds->key, header->key);

			for (k = 0; k < p->conf.response_rewrites->used; k++) {
				proxy_rewrite *rw = p->conf.response_rewrites->ptr[k];

				if (buffer_is_equal(rw->header, header->key)) {
					int ret;
	
					if ((ret = pcre_replace(rw->regex, rw->replace, header->value, p->replace_buf)) < 0) {
						switch (ret) {
						case PCRE_ERROR_NOMATCH:
							/* hmm, ok. no problem */
							buffer_append_string_buffer(ds->value, header->value);
							break;
						default:
							TRACE("oops, pcre_replace failed with: %d", ret);
							break;
						}
					} else {
						buffer_append_string_buffer(ds->value, p->replace_buf);
					}

					break;
				}
			}

			if (k == p->conf.response_rewrites->used) {
				buffer_copy_string_buffer(ds->value, header->value);
			}

			array_insert_unique(con->response.headers, (data_unset *)ds);
		}

		/* does the client allow us to send chunked encoding ? */
		if (con->request.http_version == HTTP_VERSION_1_1 &&
		    !have_content_length) {
			con->response.transfer_encoding = HTTP_TRANSFER_ENCODING_CHUNKED;
   		}

		break;
	}

	return PARSE_SUCCESS; /* we have a full header */
}


