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

/** 
 * open the errorlog
 * 
 * if the open failed, report to the user and die
 * if no filename is given, use syslog instead
 * 
 */

int log_error_open(server *srv) {
	int fd;
	int close_stderr = 1;
	
	if (srv->srvconf.error_logfile->used) {
		const char *logfile = srv->srvconf.error_logfile->ptr;
		
		if (-1 == (srv->log_error_fd = open(logfile, O_APPEND | O_WRONLY | O_CREAT | O_LARGEFILE, 0644))) {
			log_error_write(srv, __FILE__, __LINE__, "SSSS", 
					"opening errorlog '", logfile,
					"' failed: ", strerror(errno));
			
			return -1;
		}
#ifdef FD_CLOEXEC
		/* close fd on exec (cgi) */
		fcntl(srv->log_error_fd, F_SETFD, FD_CLOEXEC);
#endif
	} else {
		srv->log_error_fd = -1;
#ifdef HAVE_SYSLOG_H
		/* syslog-mode */
		srv->log_using_syslog = 1;
		openlog("lighttpd", LOG_CONS, LOG_LOCAL0);
#endif
	}
	
	log_error_write(srv, __FILE__, __LINE__, "s", "server started");
	
#ifdef HAVE_VALGRIND_VALGRIND_H
	/* don't close stderr for debugging purposes if run in valgrind */
	if (RUNNING_ON_VALGRIND) close_stderr = 0;
#endif

	/* move stderr to /dev/null */
	if (close_stderr &&
	    -1 != (fd = open("/dev/null", O_WRONLY))) {
		close(STDERR_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
	}
	return 0;
}

/** 
 * open the errorlog
 * 
 * if the open failed, report to the user and die
 * if no filename is given, use syslog instead
 * 
 */

int log_error_cycle(server *srv) {
	/* only cycle if we are not in syslog-mode */
	
	if (0 == srv->log_using_syslog) {
		const char *logfile = srv->srvconf.error_logfile->ptr;
		/* already check of opening time */
		
		int new_fd;
		
		if (-1 == (new_fd = open(logfile, O_APPEND | O_WRONLY | O_CREAT | O_LARGEFILE, 0644))) {
			/* write to old log */
			log_error_write(srv, __FILE__, __LINE__, "SSSSS", 
					"cycling errorlog '", logfile,
					"' failed: ", strerror(errno),
					", falling back to syslog()");
			
			close(srv->log_error_fd);
			srv->log_error_fd = -1;
#ifdef HAVE_SYSLOG_H	
			/* fall back to syslog() */
			srv->log_using_syslog = 1;
			
			openlog("lighttpd", LOG_CONS, LOG_LOCAL0);
#endif
		} else {
			/* ok, new log is open, close the old one */
			close(srv->log_error_fd);
			srv->log_error_fd = new_fd;
		}
	}
	
	log_error_write(srv, __FILE__, __LINE__, "s", "logfiles cycled");
	
	return 0;
}

int log_error_close(server *srv) {
	log_error_write(srv, __FILE__, __LINE__, "s", "server stopped");
	
	if (srv->log_error_fd >= 0) {
		close(srv->log_error_fd);
	} else if(srv->log_using_syslog) {
#ifdef HAVE_SYSLOG_H
		closelog();
#endif
	}
	
	return 0;
}

