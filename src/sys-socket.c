#include "sys-socket.h"

#ifndef HAVE_INET_ATON
/* win32 has inet_addr instead if inet_aton */
# ifdef HAVE_INET_ADDR
int inet_aton(const char *cp, struct in_addr *inp) {
    struct in_addr a;

    a.s_addr = inet_addr(cp);

    if (INADDR_NONE == a.s_addr) {
        return 0;
    }

    inp->s_addr = a.s_addr;

    return 1;
}
# else
#  error no inet_aton emulation found
# endif

#endif

