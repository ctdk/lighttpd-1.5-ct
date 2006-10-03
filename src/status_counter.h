#ifndef _STATUS_COUNTER_H_
#define _STATUS_COUNTER_H_

#include <sys/types.h>

#include "array.h"

void status_counter_init(void);
void status_counter_free(void);
array *status_counter_get_array(void);
data_integer *status_counter_get_counter(const char *s, size_t len);
int status_counter_inc(const char *s, size_t len);
int status_counter_dec(const char *s, size_t len);
int status_counter_set(const char *s, size_t len, int val);

#endif 
