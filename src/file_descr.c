#include <stdlib.h>
#include <unistd.h>

#include "file_descr_funcs.h"

file_descr *file_descr_init() {
	file_descr *fd;

	fd = calloc(1, sizeof(*fd));

	fd->fd = -1;
	fd->fde_ndx = -1;
	
	fd->is_socket = 1;

	return fd;
}

void file_descr_free(file_descr *fd) {
	if (!fd) return;

	free(fd);
}

void file_descr_reset(file_descr *fd) {
	if (!fd) return;

#ifdef __WIN32
	if (fd->fd) closesocket(fd->fd);
#else 
	if (fd->fd) close(fd->fd);
#endif

	fd->fd = -1;
	fd->fde_ndx = -1;
}
