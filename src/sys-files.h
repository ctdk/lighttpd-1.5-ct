#ifndef _SYS_FILES_H_
#define _SYS_FILES_H_

#define DIR_SEPERATOR_UNIX '/'
#define DIR_SEPERATOR_WIN '\\'

#ifdef _WIN32
#include <windows.h>
#include <io.h>     /* open */
#include <direct.h> /* chdir */

#include "buffer.h"

#define DIR_SEPERATOR DIR_SEPERATOR_WIN

#define __S_ISTYPE(mode, mask)  (((mode) & _S_IFMT) == (mask))

#define S_ISDIR(mode)    __S_ISTYPE((mode), _S_IFDIR)
#define S_ISCHR(mode)    __S_ISTYPE((mode), _S_IFCHR)
#define S_ISBLK(mode)    __S_ISTYPE((mode), _S_IFBLK)
#define S_ISREG(mode)    __S_ISTYPE((mode), _S_IFREG)
/* we don't support symlinks */
#define S_ISLNK(mode)    0

#define lstat stat
#define mkstemp mktemp
#define mkdir(x, y) mkdir(x)

struct dirent {
    const char *d_name;
};

typedef struct {
    HANDLE h;
    WIN32_FIND_DATA finddata;
    struct dirent dent;
} DIR;

DIR *opendir(const char *dn);
struct dirent *readdir(DIR *d);
void closedir(DIR *d);

buffer *filename_unix2local(buffer *b);
buffer *pathname_unix2local(buffer *b);

#else
#include <unistd.h>
#include <dirent.h>

#define DIR_SEPERATOR DIR_SEPERATOR_UNIX

#define filename_unix2local(x) (x)
#define pathname_unix2local(x) (x)
#endif

#define PATHNAME_APPEND_SLASH(x) \
	if (x->used > 1 && x->ptr[x->used - 2] != DIR_SEPERATOR) { \
        char sl[2] = { DIR_SEPERATOR, 0 }; \
        BUFFER_APPEND_STRING_CONST(x, sl); \
    }

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

#endif

