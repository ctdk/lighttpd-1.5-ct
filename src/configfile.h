#ifndef _CONFIG_PARSER_H_
#define _CONFIG_PARSER_H_

#include "settings.h"
#include "server.h"
#include "array.h"
#include "buffer.h"

typedef struct {
	server *srv;
	int     ok;
	array  *all_configs;
	buffer_ptr  *configs_stack; /* to parse nested block */
	data_config *current; /* current started with { */
	buffer *basedir;
} config_t;

LI_EXPORT void * configparserAlloc(void *(*mallocProc)(size_t));
LI_EXPORT void configparserFree(void *p, void (*freeProc)(void*));
LI_EXPORT void configparser(void *yyp, int yymajor, buffer *yyminor, config_t *ctx);
LI_EXPORT int config_parse_file(server *srv, config_t *context, const char *fn);
LI_EXPORT int config_parse_cmd(server *srv, config_t *context, const char *cmd);
LI_EXPORT data_unset * configparser_merge_data(data_unset *op1, const data_unset *op2);
LI_EXPORT void config_cond_cache_reset(server *srv, connection *con);

#endif
