#ifndef ETAG_H
#define ETAG_H

#include <sys/types.h>
#include <sys/stat.h>

#include "buffer.h"

LI_EXPORT int etag_is_equal(buffer *etag, const char *matches);
LI_EXPORT int etag_create(buffer *etag, struct stat *st);
LI_EXPORT int etag_mutate(buffer *mut, buffer *etag);


#endif
