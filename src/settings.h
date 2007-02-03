#ifndef _LIGHTTPD_SETTINGS_H_
#define _LIGHTTPD_SETTINGS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BV(x) (1 << x)

#define INET_NTOP_CACHE_MAX 4
#define FILE_CACHE_MAX      16

/**
 * max size of a buffer which will just be reset
 * to ->used = 0 instead of really freeing the buffer
 *
 * 64kB (no real reason, just a guess)
 */
#define BUFFER_MAX_REUSE_SIZE  (4 * 1024)

/**
 * max size of the HTTP request header
 *
 * 32k should be enough for everything (just a guess)
 *
 */
#define MAX_HTTP_REQUEST_HEADER  (32 * 1024)

#include <glib.h>

/**
 * if glib supports threads we will use it for async file-io
 */
#ifdef G_THREADS_ENABLED
# ifndef USE_GTHREAD
#  define USE_GTHREAD
# endif
#endif


/* on linux 2.4.x you get either sendfile or LFS */
#if defined HAVE_SYS_SENDFILE_H && defined HAVE_SENDFILE && (!defined _LARGEFILE_SOURCE || defined HAVE_SENDFILE64) && defined HAVE_WRITEV && defined(__linux__) && !defined HAVE_SENDFILE_BROKEN
# define USE_LINUX_SENDFILE
# include <sys/sendfile.h>
# include <sys/uio.h>
#endif

/* all the Async IO backends need GTHREAD support */
#if defined(USE_GTHREAD)
# if defined(USE_LINUX_SENDFILE) && defined(HAVE_LIBAIO_H)
#  define USE_LINUX_AIO_SENDFILE
# endif
# ifdef HAVE_AIO_H
#  define USE_POSIX_AIO
#  include <aio.h>
# endif
#endif

#if defined HAVE_SYS_UIO_H && defined HAVE_SENDFILE && defined HAVE_WRITEV && (defined(__FreeBSD__) || defined(__DragonFly__))
# define USE_FREEBSD_SENDFILE
# include <sys/uio.h>
#endif

#if defined HAVE_SYS_SENDFILE_H && defined HAVE_SENDFILEV && defined HAVE_WRITEV && defined(__sun)
# define USE_SOLARIS_SENDFILEV
# include <sys/sendfile.h>
# include <sys/uio.h>
#endif

#if defined HAVE_SYS_UIO_H && defined HAVE_WRITEV
# define USE_WRITEV
# include <sys/uio.h>
#endif

#if defined HAVE_SYS_MMAN_H && defined HAVE_MMAP
# define USE_MMAP
# include <sys/mman.h>
/* NetBSD 1.3.x needs it */
# ifndef MAP_FAILED
#  define MAP_FAILED -1
# endif
#endif

#if defined HAVE_SYS_UIO_H && defined HAVE_WRITEV && defined HAVE_SEND_FILE && defined(__aix)
# define USE_AIX_SENDFILE
#endif

/**
* unix can use read/write or recv/send on sockets
* win32 only recv/send
*/
#ifdef _WIN32
# define USE_WIN32_SEND
/* wait for async-io support
# define USE_WIN32_TRANSMITFILE
*/
#else
# define USE_WRITE
#endif


typedef enum { HANDLER_UNSET,
		HANDLER_GO_ON,
		HANDLER_FINISHED,
		HANDLER_COMEBACK,
		HANDLER_WAIT_FOR_EVENT,
		HANDLER_ERROR,
		HANDLER_WAIT_FOR_FD
} handler_t;

#endif
