#ifndef _FILE_CACHE_H_
#define _FILE_CACHE_H_

#include <sys/stat.h>
#include <time.h>

#include "buffer.h"

typedef struct {
	buffer *name;
	buffer *etag;
	
	struct stat st;
	
	int    fd;
	int    fde_ndx;
	
	char   *mmap_p;
	size_t mmap_length;
	off_t  mmap_offset;
	
	size_t in_use;
	size_t is_dirty;
	
	time_t stat_ts;
	buffer *content_type;

	int    follow_symlink;
} file_cache_entry;

typedef struct {
	file_cache_entry **ptr;
	
	size_t size;
	size_t used;
	
	buffer *dir_name;
} file_cache;

#endif
