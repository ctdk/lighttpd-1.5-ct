#define _GNU_SOURCE

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <stdarg.h>
#include <stdio.h>

#include "config.h"

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include "log.h"
#include "array.h"

#ifdef HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

int log_error_write(server *srv, const char *filename, unsigned int line, const char *fmt, ...) {
	va_list ap;
	
	if (srv->log_using_syslog == 0) {
		/* cache the generated timestamp */
		if (srv->cur_ts != srv->last_generated_debug_ts) {
			buffer_prepare_copy(srv->ts_debug_str, 255);
			strftime(srv->ts_debug_str->ptr, srv->ts_debug_str->size - 1, "%Y-%m-%d %H:%M:%S", localtime(&(srv->cur_ts)));
			srv->ts_debug_str->used = strlen(srv->ts_debug_str->ptr) + 1;
			
			srv->last_generated_debug_ts = srv->cur_ts;
		}

		buffer_copy_string_buffer(srv->error_log, srv->ts_debug_str);
		BUFFER_APPEND_STRING_CONST(srv->error_log, ": (");
	} else {
		BUFFER_COPY_STRING_CONST(srv->error_log, "(");
	}
	buffer_append_string(srv->error_log, filename);
	BUFFER_APPEND_STRING_CONST(srv->error_log, ".");
	buffer_append_long(srv->error_log, line);
	BUFFER_APPEND_STRING_CONST(srv->error_log, ") ");
	
	
	for(va_start(ap, fmt); *fmt; fmt++) {
		int d;
		char *s;
		buffer *b;
		off_t o;
		
		switch(*fmt) {
		case 's':           /* string */
			s = va_arg(ap, char *);
			buffer_append_string(srv->error_log, s);
			BUFFER_APPEND_STRING_CONST(srv->error_log, " ");
			break;
		case 'b':           /* buffer */
			b = va_arg(ap, buffer *);
			buffer_append_string_buffer(srv->error_log, b);
			BUFFER_APPEND_STRING_CONST(srv->error_log, " ");
			break;
		case 'd':           /* int */
			d = va_arg(ap, int);
			buffer_append_long(srv->error_log, d);
			BUFFER_APPEND_STRING_CONST(srv->error_log, " ");
			break;
		case 'o':           /* off_t */
			o = va_arg(ap, off_t);
			buffer_append_off_t(srv->error_log, o);
			BUFFER_APPEND_STRING_CONST(srv->error_log, " ");
			break;
		case 'x':           /* int (hex) */
			d = va_arg(ap, int);
			BUFFER_APPEND_STRING_CONST(srv->error_log, "0x");
			buffer_append_hex(srv->error_log, d);
			BUFFER_APPEND_STRING_CONST(srv->error_log, " ");
			break;
		case 'S':           /* string */
			s = va_arg(ap, char *);
			buffer_append_string(srv->error_log, s);
			break;
		case 'B':           /* buffer */
			b = va_arg(ap, buffer *);
			buffer_append_string_buffer(srv->error_log, b);
			break;
		case 'D':           /* int */
			d = va_arg(ap, int);
			buffer_append_long(srv->error_log, d);
			break;
		case '(':
		case ')':
		case '<':	
		case '>':
		case ',':
		case ' ':
			buffer_append_string_len(srv->error_log, fmt, 1);
			break;
		}
	}
	va_end(ap);
	
	BUFFER_APPEND_STRING_CONST(srv->error_log, "\n");
	
	if (srv->log_error_fd >= 0) {
		write(srv->log_error_fd, srv->error_log->ptr, srv->error_log->used - 1);
	} else if (srv->log_using_syslog == 0) {
		/* only available at startup time */
		write(STDERR_FILENO, srv->error_log->ptr, srv->error_log->used - 1);
	} else {
#ifdef HAVE_SYSLOG_H
		syslog(LOG_ERR, "%s", srv->error_log->ptr);
#endif
	}
	
	return 0;
}

