#ifndef _NETWORK_BACKENDS_H_
#define _NETWORK_BACKENDS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

#include "base.h"
#include "network.h"

#define NETWORK_BACKEND_WRITE_CHUNK(x) \
    LI_EXPORT network_status_t network_write_chunkqueue_##x(server *srv, connection *con, iosocket *sock, chunkqueue *cq, chunk *c)

#define NETWORK_BACKEND_WRITE(x) \
    LI_EXPORT network_status_t network_write_chunkqueue_##x(server *srv, connection *con, iosocket *sock, chunkqueue *cq)
#define NETWORK_BACKEND_READ(x) \
    LI_EXPORT network_status_t network_read_chunkqueue_##x(server *srv, connection *con, iosocket *sock, chunkqueue *cq)

NETWORK_BACKEND_WRITE_CHUNK(writev_mem);

NETWORK_BACKEND_WRITE(write);
NETWORK_BACKEND_WRITE(writev);
NETWORK_BACKEND_WRITE(linuxsendfile);
NETWORK_BACKEND_WRITE(linuxaiosendfile);
NETWORK_BACKEND_WRITE(posixaio);
NETWORK_BACKEND_WRITE(gthreadaio);
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
