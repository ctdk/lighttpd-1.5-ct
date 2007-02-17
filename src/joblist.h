#ifndef _JOB_LIST_H_
#define _JOB_LIST_H_

#include "base.h"

LI_EXPORT int joblist_append(server *srv, connection *con);
LI_EXPORT void joblist_free(server *srv, connections *joblist);

LI_EXPORT int fdwaitqueue_append(server *srv, connection *con);
LI_EXPORT void fdwaitqueue_free(server *srv, connections *fdwaitqueue);
LI_EXPORT connection* fdwaitqueue_unshift(server *srv, connections *fdwaitqueue);

#endif
