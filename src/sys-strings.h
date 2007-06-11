#ifndef _SYS_STRINGS_H_
#define _SYS_STRINGS_H_

#ifdef _WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#include <stdlib.h>
#define strtoll(p, e, b) _strtoi64(p, e, b)
#define strtoull _strtoui64
#ifdef __MINGW32__
/* missing prototype */
unsigned __int64 _strtoui64(
		const char *nptr,
		char **endptr,
		int base 
		);
__int64 _strtoi64(
		const char *nptr,
		char **endptr,
		int base 
		);
#endif
#endif

#endif

