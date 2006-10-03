#ifndef _NETWORK_BACKENDS_H_
#define _NETWORK_BACKENDS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

/* on linux 2.4.x you get either sendfile or LFS */
#if defined HAVE_SYS_SENDFILE_H && defined HAVE_SENDFILE && (!defined _LARGEFILE_SOURCE || defined HAVE_SENDFILE64) && defined HAVE_WRITEV && defined(__linux__) && !defined HAVE_SENDFILE_BROKEN
# define USE_LINUX_SENDFILE
# include <sys/sendfile.h>
# include <sys/uio.h>
#endif

#if defined HAVE_SYS_UIO_H && defined HAVE_SENDFILE && defined HAVE_WRITEV && defined(__FreeBSD__)
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

#include "base.h"
#include "network.h"

#define NETWORK_BACKEND_WRITE_CHUNK(x) \
    network_status_t network_write_chunkqueue_##x(server *srv, connection *con, iosocket *sock, chunkqueue *cq, chunk *c)

#define NETWORK_BACKEND_WRITE(x) \
    network_status_t network_write_chunkqueue_##x(server *srv, connection *con, iosocket *sock, chunkqueue *cq)
#define NETWORK_BACKEND_READ(x) \
    network_status_t network_read_chunkqueue_##x(server *srv, connection *con, iosocket *sock, chunkqueue *cq)

NETWORK_BACKEND_WRITE_CHUNK(writev_mem);

NETWORK_BACKEND_WRITE(write);
NETWORK_BACKEND_WRITE(writev);
NETWORK_BACKEND_WRITE(linuxsendfile);
NETWORK_BACKEND_WRITE(freebsdsendfile);
NETWORK_BACKEND_WRITE(solarissendfilev);

NETWORK_BACKEND_WRITE(win32transmitfile);
NETWORK_BACKEND_WRITE(win32send);

NETWORK_BACKEND_READ(read);
NETWORK_BACKEND_READ(win32recv);

#ifdef USE_OPENSSL
NETWORK_BACKEND_WRITE(openssl);
NETWORK_BACKEND_READ(openssl);
#endif

#endif
