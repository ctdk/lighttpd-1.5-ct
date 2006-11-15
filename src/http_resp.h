#ifndef _HTTP_RESP_H_
#define _HTTP_RESP_H_

#include "buffer.h"
#include "array.h"
#include "array-static.h"
#include "chunk.h"
#include "http_parser.h"

typedef struct {
	int protocol;   /* http/1.0, http/1.1 */
	int status;     /* e.g. 200 */
	buffer *reason; /* e.g. Ok */
	array *headers;
} http_resp;

/**
 * a pool of unused buffer * 
 */

ARRAY_STATIC_DEF(buffer_pool, buffer, );

typedef struct {
	int     ok;
	buffer *errmsg;

	http_resp *resp;

	buffer_pool *unused_buffers;
} http_resp_ctx_t;

http_resp *http_response_init(void);
void http_response_free(http_resp *resp);
void http_response_reset(http_resp *resp);

parse_status_t http_response_parse_cq(chunkqueue *cq, http_resp *http_response);

buffer_pool* buffer_pool_init();
void buffer_pool_free(buffer_pool* );

buffer *buffer_pool_get(buffer_pool *bp);
void buffer_pool_append(buffer_pool *bp, buffer *);

#endif
