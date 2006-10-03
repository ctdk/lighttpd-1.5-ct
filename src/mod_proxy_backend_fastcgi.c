#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "inet_ntop_cache.h"
#include "mod_proxy_core.h"
#include "buffer.h"
#include "log.h"
#include "fastcgi.h"

int proxy_fastcgi_get_env_fastcgi(server *srv, connection *con, plugin_data *p, proxy_session *sess) {
	buffer *b;

	char buf[32];
	const char *s;
	server_socket *srv_sock = con->srv_socket;
#ifdef HAVE_IPV6
	char b2[INET6_ADDRSTRLEN + 1];
#endif

	sock_addr our_addr;
	socklen_t our_addr_len;

	proxy_set_header(sess->env_headers, CONST_STR_LEN("SERVER_SOFTWARE"), CONST_STR_LEN(PACKAGE_NAME"/"PACKAGE_VERSION));

	if (con->server_name->used) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("SERVER_NAME"), CONST_BUF_LEN(con->server_name));
	} else {
#ifdef HAVE_IPV6
		s = inet_ntop(srv_sock->addr.plain.sa_family,
			      srv_sock->addr.plain.sa_family == AF_INET6 ?
			      (const void *) &(srv_sock->addr.ipv6.sin6_addr) :
			      (const void *) &(srv_sock->addr.ipv4.sin_addr),
			      b2, sizeof(b2)-1);
#else
		s = inet_ntoa(srv_sock->addr.ipv4.sin_addr);
#endif
		proxy_set_header(sess->env_headers, CONST_STR_LEN("SERVER_NAME"), s, strlen(s));
	}

	proxy_set_header(sess->env_headers, CONST_STR_LEN("GATEWAY_INTERFACE"), CONST_STR_LEN("CGI/1.1"));

	ltostr(buf,
#ifdef HAVE_IPV6
	       ntohs(srv_sock->addr.plain.sa_family ? srv_sock->addr.ipv6.sin6_port : srv_sock->addr.ipv4.sin_port)
#else
	       ntohs(srv_sock->addr.ipv4.sin_port)
#endif
	       );

	proxy_set_header(sess->env_headers, CONST_STR_LEN("SERVER_PORT"), buf, strlen(buf));

	/* get the server-side of the connection to the client */
	our_addr_len = sizeof(our_addr);

	if (-1 == getsockname(con->sock->fd, &(our_addr.plain), &our_addr_len)) {
		s = inet_ntop_cache_get_ip(srv, &(srv_sock->addr));
	} else {
		s = inet_ntop_cache_get_ip(srv, &(our_addr));
	}
	proxy_set_header(sess->env_headers, CONST_STR_LEN("SERVER_ADDR"), s, strlen(s));

	ltostr(buf,
#ifdef HAVE_IPV6
	       ntohs(con->dst_addr.plain.sa_family ? con->dst_addr.ipv6.sin6_port : con->dst_addr.ipv4.sin_port)
#else
	       ntohs(con->dst_addr.ipv4.sin_port)
#endif
	       );

	proxy_set_header(sess->env_headers, CONST_STR_LEN("REMOTE_PORT"), buf, strlen(buf));

	s = inet_ntop_cache_get_ip(srv, &(con->dst_addr));
	proxy_set_header(sess->env_headers, CONST_STR_LEN("REMOTE_ADDR"), s, strlen(s));

	if (!buffer_is_empty(con->authed_user)) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("REMOTE_USER"),
			     CONST_BUF_LEN(con->authed_user));
	}

	if (con->request.content_length > 0) {
		/* CGI-SPEC 6.1.2 and FastCGI spec 6.3 */

		/* request.content_length < SSIZE_MAX, see request.c */
		ltostr(buf, con->request.content_length);
		proxy_set_header(sess->env_headers, CONST_STR_LEN("CONTENT_LENGTH"), buf, strlen(buf));
	}

	
	/*
	 * SCRIPT_NAME, PATH_INFO and PATH_TRANSLATED according to
	 * http://cgi-spec.golux.com/draft-coar-cgi-v11-03-clean.html
	 * (6.1.14, 6.1.6, 6.1.7)
	 * For AUTHORIZER mode these headers should be omitted.
	 */

	proxy_set_header(sess->env_headers, CONST_STR_LEN("SCRIPT_NAME"), CONST_BUF_LEN(con->uri.path));

	if (!buffer_is_empty(con->request.pathinfo)) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("PATH_INFO"), CONST_BUF_LEN(con->request.pathinfo));

		/* PATH_TRANSLATED is only defined if PATH_INFO is set */

		buffer_copy_string_buffer(p->tmp_buf, con->physical.doc_root);
		buffer_append_string_buffer(p->tmp_buf, con->request.pathinfo);
		proxy_set_header(sess->env_headers, CONST_STR_LEN("PATH_TRANSLATED"), CONST_BUF_LEN(p->tmp_buf));
	} else {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("PATH_INFO"), CONST_STR_LEN(""));
	}

	/*
	 * SCRIPT_FILENAME and DOCUMENT_ROOT for php. The PHP manual
	 * http://www.php.net/manual/en/reserved.variables.php
	 * treatment of PATH_TRANSLATED is different from the one of CGI specs.
	 * TODO: this code should be checked against cgi.fix_pathinfo php
	 * parameter.
	 */

	if (1) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("SCRIPT_FILENAME"), CONST_BUF_LEN(con->physical.path));
		proxy_set_header(sess->env_headers, CONST_STR_LEN("DOCUMENT_ROOT"), CONST_BUF_LEN(con->physical.doc_root));
	}

	proxy_set_header(sess->env_headers, CONST_STR_LEN("REQUEST_URI"), CONST_BUF_LEN(con->request.orig_uri));

	if (!buffer_is_equal(con->request.uri, con->request.orig_uri)) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("REDIRECT_URI"), CONST_BUF_LEN(con->request.uri));
	}
	if (!buffer_is_empty(con->uri.query)) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("QUERY_STRING"), CONST_BUF_LEN(con->uri.query));
	} else {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("QUERY_STRING"), CONST_STR_LEN(""));
	}

	s = get_http_method_name(con->request.http_method);
	proxy_set_header(sess->env_headers, CONST_STR_LEN("REQUEST_METHOD"), s, strlen(s));
	proxy_set_header(sess->env_headers, CONST_STR_LEN("REDIRECT_STATUS"), CONST_STR_LEN("200")); /* if php is compiled with --force-redirect */
	s = get_http_version_name(con->request.http_version);
	proxy_set_header(sess->env_headers, CONST_STR_LEN("SERVER_PROTOCOL"), s, strlen(s));

#ifdef USE_OPENSSL
	if (srv_sock->is_ssl) {
		proxy_set_header(sess->env_headers, CONST_STR_LEN("HTTPS"), CONST_STR_LEN("on"));
	}
#endif

	return 0;
}

/**
 * transform the HTTP-Request headers into CGI notation
 */
int proxy_fastcgi_get_env_request(server *srv, connection *con, plugin_data *p, proxy_session *sess) {
	size_t i;
	/* the request header got already copied into the sess->request_headers for us
	 * no extra filter is needed
	 *
	 * prepend a HTTP_ and uppercase the keys
	 */
	for (i = 0; i < sess->request_headers->used; i++) {
		data_string *ds;
		size_t j;

		ds = (data_string *)sess->request_headers->data[i];

		buffer_reset(p->tmp_buf);

		if (0 != strcasecmp(ds->key->ptr, "CONTENT-TYPE")) {
			BUFFER_COPY_STRING_CONST(p->tmp_buf, "HTTP_");
			p->tmp_buf->used--;
		}

		buffer_prepare_append(p->tmp_buf, ds->key->used + 2);
		for (j = 0; j < ds->key->used - 1; j++) {
			char c = '_';
			if (light_isalpha(ds->key->ptr[j])) {
				/* upper-case */
				c = ds->key->ptr[j] & ~32;
			} else if (light_isdigit(ds->key->ptr[j])) {
				/* copy */
				c = ds->key->ptr[j];
			}
			p->tmp_buf->ptr[p->tmp_buf->used++] = c;
		}
		p->tmp_buf->ptr[p->tmp_buf->used++] = '\0';

		proxy_set_header(sess->env_headers, CONST_BUF_LEN(p->tmp_buf), CONST_BUF_LEN(ds->value));
	}

	return 0;
}


/**
 * add a key-value pair to the fastcgi-buffer
 */

static int fcgi_env_add(buffer *env, const char *key, size_t key_len, const char *val, size_t val_len) {
	size_t len;

	if (!key || !val) return -1;

	len = key_len + val_len;

	len += key_len > 127 ? 4 : 1;
	len += val_len > 127 ? 4 : 1;

	buffer_prepare_append(env, len);

	if (key_len > 127) {
		env->ptr[env->used++] = ((key_len >> 24) & 0xff) | 0x80;
		env->ptr[env->used++] = (key_len >> 16) & 0xff;
		env->ptr[env->used++] = (key_len >> 8) & 0xff;
		env->ptr[env->used++] = (key_len >> 0) & 0xff;
	} else {
		env->ptr[env->used++] = (key_len >> 0) & 0xff;
	}

	if (val_len > 127) {
		env->ptr[env->used++] = ((val_len >> 24) & 0xff) | 0x80;
		env->ptr[env->used++] = (val_len >> 16) & 0xff;
		env->ptr[env->used++] = (val_len >> 8) & 0xff;
		env->ptr[env->used++] = (val_len >> 0) & 0xff;
	} else {
		env->ptr[env->used++] = (val_len >> 0) & 0xff;
	}

	memcpy(env->ptr + env->used, key, key_len);
	env->used += key_len;
	memcpy(env->ptr + env->used, val, val_len);
	env->used += val_len;

	return 0;
}

/**
 * init the FCGI_header 
 */
static int fcgi_header(FCGI_Header * header, unsigned char type, size_t request_id, int contentLength, unsigned char paddingLength) {
	header->version = FCGI_VERSION_1;
	header->type = type;
	header->requestIdB0 = request_id & 0xff;
	header->requestIdB1 = (request_id >> 8) & 0xff;
	header->contentLengthB0 = contentLength & 0xff;
	header->contentLengthB1 = (contentLength >> 8) & 0xff;
	header->paddingLength = paddingLength;
	header->reserved = 0;

	return 0;
}


int proxy_fastcgi_get_request_chunk(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *out) {
	buffer *b, *packet;
	size_t i;
	FCGI_BeginRequestRecord beginRecord;
	FCGI_Header header;
	int request_id = 1;

	b = chunkqueue_get_append_buffer(out);
	/* send FCGI_BEGIN_REQUEST */

	fcgi_header(&(beginRecord.header), FCGI_BEGIN_REQUEST, FCGI_NULL_REQUEST_ID, sizeof(beginRecord.body), 0);
	beginRecord.body.roleB0 = FCGI_RESPONDER;
	beginRecord.body.roleB1 = 0;
	beginRecord.body.flags = 0;
	memset(beginRecord.body.reserved, 0, sizeof(beginRecord.body.reserved));

	buffer_copy_string_len(b, (const char *)&beginRecord, sizeof(beginRecord));
	out->bytes_in += sizeof(beginRecord);

	/* send FCGI_PARAMS */
	b = chunkqueue_get_append_buffer(out);
	buffer_prepare_copy(b, 1024);

	/* fill the sess->env_headers */
	array_reset(sess->env_headers);
	proxy_fastcgi_get_env_request(srv, con, p, sess);
	proxy_fastcgi_get_env_fastcgi(srv, con, p, sess);

	packet = buffer_init();

	for (i = 0; i < sess->env_headers->used; i++) {
		data_string *ds;

		ds = (data_string *)sess->env_headers->data[i];
		fcgi_env_add(packet, CONST_BUF_LEN(ds->key), CONST_BUF_LEN(ds->value));
	}

	fcgi_header(&(header), FCGI_PARAMS, FCGI_NULL_REQUEST_ID, packet->used, 0);
	buffer_append_memory(b, (const char *)&header, sizeof(header));
	buffer_append_memory(b, (const char *)packet->ptr, packet->used);
	out->bytes_in += sizeof(header);
	out->bytes_in += packet->used - 1;

	buffer_free(packet);

	fcgi_header(&(header), FCGI_PARAMS, FCGI_NULL_REQUEST_ID, 0, 0);
	buffer_append_memory(b, (const char *)&header, sizeof(header));
	out->bytes_in += sizeof(header);

	return 0;
}


typedef struct {
	buffer  *b;
	size_t   len;
	int      type;
	int      padding;
	size_t   request_id;
} fastcgi_response_packet;

int proxy_fastcgi_stream_decoder(server *srv, proxy_session *sess, chunkqueue *raw, chunkqueue *decoded) {
	chunk *	c;
	size_t offset = 0;
	size_t toread = 0;
	FCGI_Header *header;
	fastcgi_response_packet packet;
	buffer *b;

	if (!raw->first) return 0;

	packet.b = buffer_init();
	packet.len = 0;
	packet.type = 0;
	packet.padding = 0;
	packet.request_id = 0;

	/* get at least the FastCGI header */
	for (c = raw->first; c; c = c->next) {
		if (packet.b->used == 0) {
			buffer_copy_string_len(packet.b, c->mem->ptr + c->offset, c->mem->used - c->offset - 1);
		} else {
			buffer_append_string_len(packet.b, c->mem->ptr + c->offset, c->mem->used - c->offset - 1);
		}

		if (packet.b->used >= sizeof(*header) + 1) break;
	}

	if ((packet.b->used == 0) ||
	    (packet.b->used - 1 < sizeof(FCGI_Header))) {
		/* no header */
		buffer_free(packet.b);

		ERROR("%s", "FastCGI: header too small");
		return -1;
	}

	/* we have at least a header, now check how much me have to fetch */
	header = (FCGI_Header *)(packet.b->ptr);

	packet.len = (header->contentLengthB0 | (header->contentLengthB1 << 8)) + header->paddingLength;
	packet.request_id = (header->requestIdB0 | (header->requestIdB1 << 8));
	packet.type = header->type;
	packet.padding = header->paddingLength;

	/* the first bytes in packet->b are the header */
	offset = sizeof(*header);

	buffer_copy_string(packet.b, "");

	if (packet.len) {
		/* copy the content */
		for (; c && (packet.b->used < packet.len + 1); c = c->next) {
			size_t weWant = packet.len - (packet.b->used - 1);
			size_t weHave = c->mem->used - c->offset - offset - 1;

			if (weHave > weWant) weHave = weWant;

			buffer_append_string_len(packet.b, c->mem->ptr + c->offset + offset, weHave);

			/* we only skipped the first 8 bytes as they are the fcgi header */
			offset = 0;
		}

		if (packet.b->used < packet.len + 1) {
			/* we didn't got the full packet */

			buffer_free(packet.b);

			TRACE("%s", "need more");

			return 0;
		}

		packet.b->used -= packet.padding;
		packet.b->ptr[packet.b->used - 1] = '\0';
	}

	/* tag the chunks as read */
	toread = packet.len + sizeof(FCGI_Header);
	for (c = raw->first; c && toread; c = c->next) {
		if (c->mem->used - c->offset - 1 <= toread) {
			/* we read this whole buffer, move it to unused */
			toread -= c->mem->used - c->offset - 1;
			c->offset = c->mem->used - 1; /* everthing has been written */
		} else {
			c->offset += toread;
			toread = 0;
		}
	}

	chunkqueue_remove_finished_chunks(raw);

	/* we are still here ? */

	switch (packet.type) {
	case FCGI_STDOUT:
		b = chunkqueue_get_append_buffer(decoded);
		buffer_copy_string_buffer(b, packet.b);
		buffer_free(packet.b);
		return 0;
	case FCGI_STDERR:
		if (!buffer_is_empty(packet.b)) {
			TRACE("(fastcgi-stderr) %s", BUF_STR(packet.b));
		}
		buffer_free(packet.b);
		return 0;
	case FCGI_END_REQUEST:
		buffer_free(packet.b);
		return 1;
	default:
		buffer_free(packet.b);

		TRACE("unknown packet.type: %d", packet.type);
		return -1;
	}
}

/**
 * transform the content-stream into a valid HTTP-content-stream
 *
 * as we don't apply chunked-encoding here, pass it on AS IS
 */
int proxy_fastcgi_stream_encoder(server *srv, proxy_session *sess, chunkqueue *in, chunkqueue *out) {
	chunk *c;
	buffer *b;
	FCGI_Header header;

	/* there is nothing that we have to send out anymore */
	for (c = in->first; in->bytes_out < in->bytes_in; ) {
		off_t weWant = in->bytes_in - in->bytes_out > FCGI_MAX_LENGTH ? FCGI_MAX_LENGTH : in->bytes_in - in->bytes_out;
		off_t weHave = 0;

		/* we announce toWrite octects
		 * now take all the request_content chunk that we need to fill this request
		 */
		b = chunkqueue_get_append_buffer(out);
		fcgi_header(&(header), FCGI_STDIN, FCGI_NULL_REQUEST_ID, weWant, 0);
		buffer_copy_memory(b, (const char *)&header, sizeof(header) + 1);
		out->bytes_in += sizeof(header);

		switch (c->type) {
		case FILE_CHUNK:
			weHave = c->file.length - c->offset;

			if (weHave > weWant) weHave = weWant;

			chunkqueue_append_file(out, c->file.name, c->offset, weHave);

			c->offset += weHave;
			in->bytes_out += weHave;

			out->bytes_in += weHave;

			/* steal the tempfile
			 *
			 * This is tricky:
			 * - we reference the tempfile from the in-queue several times
			 *   if the chunk is larger than FCGI_MAX_LENGTH
			 * - we can't simply cleanup the in-queue as soon as possible
			 *   as it would remove the tempfiles
			 * - the idea is to 'steal' the tempfiles and attach the is_temp flag to the last
			 *   referencing chunk of the fastcgi-write-queue
			 *
			 */

			if (c->offset == c->file.length) {
				chunk *out_c;

				out_c = out->last;

				/* the last of the out-queue should be a FILE_CHUNK (we just created it)
				 * and the incoming side should have given use a temp-file-chunk */
				assert(out_c->type == FILE_CHUNK);
				assert(c->file.is_temp == 1);

				out_c->file.is_temp = 1;
				c->file.is_temp = 0;

				c = c->next;
			}

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

			if (c->offset == c->mem->used - 1) {
				c = c->next;
			}

			break;
		default:
			break;
		}
	}

	if (in->bytes_in == in->bytes_out && in->is_closed && !out->is_closed) {
		/* send the closing packet */
		b = chunkqueue_get_append_buffer(out);
		/* terminate STDIN */
		fcgi_header(&(header), FCGI_STDIN, FCGI_NULL_REQUEST_ID, 0, 0);
		buffer_copy_memory(b, (const char *)&header, sizeof(header) + 1);

		out->bytes_in += sizeof(header);
		out->is_closed = 1;
	}

	return 0;

}

/**
 * parse the response header
 *
 * NOTE: this can be used by all backends as they all send a HTTP-Response a clean block
 * - fastcgi needs some decoding for the protocol
 */
parse_status_t proxy_fastcgi_parse_response_header(server *srv, connection *con, plugin_data *p, proxy_session *sess, chunkqueue *cq) {
	int have_content_length = 0;
	size_t i;
	off_t old_len;

	http_response_reset(p->resp);

	/* decode the whole packet stream */
	do {
		old_len = chunkqueue_length(sess->recv);
		/* decode the packet */
		switch (proxy_fastcgi_stream_decoder(srv, sess, cq, sess->recv)) {
		case 0:
			/* STDERR + STDOUT */
			break;
		case 1:
			/* the FIN packet was catched, why ever */
			return PARSE_ERROR;
		case -1:
			return PARSE_ERROR;
		}
	} while (chunkqueue_length(sess->recv) == old_len);
	
	switch (http_response_parse_cq(sess->recv, p->resp)) {
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
			const char *ign[] = { "Status", "Connection", NULL };
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
			} else if (0 == buffer_caseless_compare(CONST_BUF_LEN(header->key), CONST_STR_LEN("Transfer-Encoding"))) {
				if (strstr(header->value->ptr, "chunked")) {
					sess->is_chunked = 1;
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

		break;
	}

	return PARSE_SUCCESS; /* we have a full header */
}


