#ifndef _ARRAY_STATIC_H_
#define _ARRAY_STATIC_H_

/* define a generic array of <type>
 * */

#define ARRAY_STATIC_DEF(name, type, extra) \
typedef struct { \
	type **ptr; \
	size_t used; \
	size_t size; \
	extra\
} name

/* all append operations need a 'resize' for the +1 */

#define ARRAY_STATIC_PREPARE_APPEND(a) \
        if (a->size == 0) { \
	        a->size = 16; \
	        a->ptr = malloc(a->size * sizeof(*(a->ptr))); \
	} else if (a->size == a->used) { \
		a->size += 16; \
		a->ptr = realloc(a->ptr, a->size * sizeof(*(a->ptr))); \
	}
	
#define FOREACH(array, element, func) \
do { size_t _i; for (_i = 0; _i < array->used; _i++) { void *element = array->ptr[_i]; func; } } while(0);

#define STRUCT_INIT(type, var) \
	type *var;\
	var = calloc(1, sizeof(*var))

#endif
