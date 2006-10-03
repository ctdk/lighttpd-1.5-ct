#ifndef WIN32_SOCKET_H
#define WIN32_SOCKET_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32

#include <winsock2.h>

#define ECONNRESET WSAECONNRESET
#define EINPROGRESS WSAEINPROGRESS
#define EALREADY WSAEALREADY
#define ENOTCONN WSAENOTCONN
#define EWOULDBLOCK WSAEWOULDBLOCK
#define ioctl ioctlsocket
#define hstrerror(x) ""
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define ssize_t int

int inet_aton(const char *cp, struct in_addr *inp);
#define HAVE_INET_ADDR
#undef HAVE_INET_ATON

#else
#include <sys/types.h> /* required by netinet/tcp.h on FreeBSD */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <arpa/inet.h>

#ifndef SUN_LEN
#define SUN_LEN(su) \
        (sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif

#define closesocket(x) close(x)

#include <netdb.h>
#endif /* !_WIN32 */

#ifdef HAVE_INET_NTOP
/* only define it if it isn't defined yet */
#ifndef HAVE_IPV6
#define HAVE_IPV6
#endif
#endif

typedef union {
#ifdef HAVE_IPV6
	struct sockaddr_in6 ipv6;
#endif
	struct sockaddr_in ipv4;
#ifdef HAVE_SYS_UN_H
	struct sockaddr_un un;
#endif
	struct sockaddr plain;
} sock_addr;

#endif
