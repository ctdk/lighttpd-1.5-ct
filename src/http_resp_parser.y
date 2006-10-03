%token_prefix TK_
%token_type {buffer *}
%extra_argument {http_resp_ctx_t *ctx}
%name http_resp_parser

%include {
#include <assert.h>
#include <string.h>
#include "http_resp.h"
#include "keyvalue.h"
#include "array.h"
#include "log.h"
}

%parse_failure {
  ctx->ok = 0;
}

%type protocol { int }
%type response_hdr { http_resp * }
%type number { int }
%type headers { array * }
%type header { data_string * }
%destructor reason { buffer_free($$); }
%token_destructor { buffer_free($$); }

/* just headers + Status: ... */
response_hdr ::= headers(HDR) CRLF . {
    http_resp *resp = ctx->resp;
    data_string *ds;
 
    resp->protocol = HTTP_VERSION_UNSET;

    buffer_copy_string(resp->reason, ""); /* no reason */
    array_free(resp->headers);
    resp->headers = HDR;

    if (NULL == (ds = (data_string *)array_get_element(HDR, "Status"))) { 
        resp->status = 0;
    } else {
        char *err;
        resp->status = strtol(ds->value->ptr, &err, 10);
   
        if (*err != '\0' && *err != ' ') {
            buffer_copy_string(ctx->errmsg, "expected a number: ");
            buffer_append_string_buffer(ctx->errmsg, ds->value);
            buffer_append_string(ctx->errmsg, err);
        
            ctx->ok = 0;
        }
    }

    HDR = NULL;
}
/* HTTP/1.0 <status> ... */
response_hdr ::= protocol(B) number(C) reason(D) CRLF headers(HDR) CRLF . {
    http_resp *resp = ctx->resp;
    
    resp->status = C;
    resp->protocol = B;
    buffer_copy_string_buffer(resp->reason, D);
    buffer_free(D); 

    array_free(resp->headers);
    
    resp->headers = HDR;
}

protocol(A) ::= STRING(B). {
    if (buffer_is_equal_string(B, CONST_STR_LEN("HTTP/1.0"))) {
        A = HTTP_VERSION_1_0;
    } else if (buffer_is_equal_string(B, CONST_STR_LEN("HTTP/1.1"))) {
        A = HTTP_VERSION_1_1;
    } else {
        buffer_copy_string(ctx->errmsg, "unknown protocol: ");
        buffer_append_string_buffer(ctx->errmsg, B);
        
        ctx->ok = 0;
    }
    buffer_free(B);
}

number(A) ::= STRING(B). {
    char *err;
    A = strtol(B->ptr, &err, 10);
    
    if (*err != '\0') {
        buffer_copy_string(ctx->errmsg, "expected a number, got: ");
        buffer_append_string_buffer(ctx->errmsg, B);
        
        ctx->ok = 0;
    }
    buffer_free(B);
}

reason(A) ::= STRING(B). {
    A = B;
}

reason(A) ::= reason(C) STRING(B). {
    A = C;
    
    buffer_append_string(A, " ");
    buffer_append_string_buffer(A, B);

    buffer_free(B); 
}

headers(HDRS) ::= headers(SRC) header(HDR). {
    HDRS = SRC;
    
    array_insert_unique(HDRS, (data_unset *)HDR);
}

headers(HDRS) ::= header(HDR). {
    HDRS = array_init();

    array_insert_unique(HDRS, (data_unset *)HDR);
}
header(HDR) ::= STRING(A) COLON STRING(B) CRLF. {
    HDR = data_response_init();
    
    buffer_copy_string_buffer(HDR->key, A);
    buffer_copy_string_buffer(HDR->value, B);    
    buffer_free(A);
    buffer_free(B);
}
