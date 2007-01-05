#ifndef _FILTER_H_
#define _FILTER_H_

#include "chunk.h"

#define FILTER_ID_INPUT		-1

/*
 * The filter chain will always have an input filter to hold
 * the pre-filtered content.  All other filters are added after
 * the last filter.
 *
 * Each filter uses the chunkqueue from the previous filter as
 * input and outputs to it's own chunkqueue.
 *
 */

typedef struct filter {
	struct filter *prev;
	struct filter *next;

	int id; /* use plugin id */
	chunkqueue *cq; /* this filters output */
} filter;

typedef struct {
	filter *first;
	filter *last;

} filter_chain;

filter_chain *filter_chain_init(void);
void filter_chain_free(filter_chain *chain);
void filter_chain_reset(filter_chain *chain);

filter *filter_chain_create_filter(filter_chain *chain, int id);
filter *filter_chain_get_filter(filter_chain *chain, int id);

int filter_chain_copy_output(filter_chain *chain, chunkqueue *out);

#endif
