#ifndef _FILE_DESCR_H_
#define _FILE_DESCR_H_

#include "config.h"

#if defined HAVE_LIBSSL && defined HAVE_OPENSSL_SSL_H
# define USE_OPENSSL
# include <openssl/ssl.h> 
#endif

typedef enum {
	NETWORK_UNSET,
	NETWORK_OK,
	NETWORK_ERROR,
	NETWORK_REMOTE_CLOSE,
	NETWORK_QUEUE_EMPTY
} network_t;

typedef struct {
	int fd;
	int fde_ndx;

	size_t bytes_read;
	size_t bytes_written;

	int is_readable;
	int is_writable;
	
	int is_socket;

	network_t (*write_func)(void *srv, void *fd, void *cq);
	network_t (*read_func)(void *srv, void *fd, void *cq);

#ifdef USE_OPENSSL
	SSL *ssl;
#endif
} file_descr;

#endif
