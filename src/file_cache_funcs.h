#ifndef _FILE_CACHE_FUNCS_H_
#define _FILE_CACHE_FUNCS_H_

#include "file_cache.h"
#include "base.h"

file_cache *file_cache_init(void);
void file_cache_free(server *srv, file_cache *fc);

file_cache_entry *file_cache_get_entry(server *srv, buffer *name);
handler_t file_cache_add_entry(server *srv, connection *con, buffer *name, file_cache_entry **fce);
int file_cache_release_entry(server *srv, file_cache_entry *fce);

handler_t file_cache_check_entry(server *srv, file_cache_entry *fce);

#endif
