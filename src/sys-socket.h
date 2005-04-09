#ifndef WIN32_SOCKET_H
#define WIN32_SOCKET_H

#ifdef __WIN32

#include <winsock2.h>

#define ECONNRESET WSAECONNRESET
#define EINPROGRESS WSAEINPROGRESS
#define ENOTCONN WSAENOTCONN
#define EALREADY WSAEALREADY
#define ioctl ioctlsocket
#define hstrerror(x) ""
/* getoptsock on win32 needs a char * for socket_error */
#define GETSOCKOPT_PARAM4_TYPE (char *)

#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <netdb.h>
#define GETSOCKOPT_PARAM4_TYPE 
#endif

#endif
