#ifndef _FILE_DESCR_FUNCS_H_
#define _FILE_DESCR_FUNCS_H_

#include "file_descr.h"

file_descr *file_descr_init();
void file_descr_free(file_descr *fd);
void file_descr_reset(file_descr *fd);

#endif
