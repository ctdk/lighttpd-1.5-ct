#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <gdbm.h>

#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"
#include "response.h"
#include "inet_ntop_cache.h"

#include "config.h"

/**
 * this is a trigger_b4_dl for a lighttpd plugin
 * 
 */

/* plugin config for all request/connections */

typedef struct {
	buffer *db_filename;
	
	buffer *trigger_url;
	buffer *download_url;
	buffer *deny_url;
	
	pcre *trigger_regex;
	pcre *download_regex;
	
	GDBM_FILE db;
	
	unsigned short trigger_timeout;
} plugin_config;

typedef struct {
	PLUGIN_DATA;
	
	buffer *match_buf;
	
	
	
	plugin_config **config_storage;
	
	plugin_config conf; 
} plugin_data;

/* init the plugin data */
INIT_FUNC(mod_trigger_b4_dl_init) {
	plugin_data *p;
	
	p = calloc(1, sizeof(*p));
	
	p->match_buf = buffer_init();
	
	
	return p;
}

/* detroy the plugin data */
FREE_FUNC(mod_trigger_b4_dl_free) {
	plugin_data *p = p_d;
	
	UNUSED(srv);

	if (!p) return HANDLER_GO_ON;
	
	if (p->config_storage) {
		size_t i;
		for (i = 0; i < srv->config_context->used; i++) {
			plugin_config *s = p->config_storage[i];
			
			buffer_free(s->db_filename);
			buffer_free(s->download_url);
			buffer_free(s->trigger_url);
			buffer_free(s->deny_url);
			
			if (s->db) {
				gdbm_close(s->db);
			}
			
			free(s);
		}
		free(p->config_storage);
	}
	
	buffer_free(p->match_buf);
	
	free(p);
	
	return HANDLER_GO_ON;
}

/* handle plugin config and check values */

SETDEFAULTS_FUNC(mod_trigger_b4_dl_set_defaults) {
	plugin_data *p = p_d;
	size_t i = 0;
	
	
	config_values_t cv[] = { 
		{ "trigger-before-download.db-filename",     NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },       /* 0 */
		{ "trigger-before-download.trigger-url",     NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },       /* 1 */
		{ "trigger-before-download.download-url",    NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },       /* 2 */
		{ "trigger-before-download.deny-url",        NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },       /* 3 */
		{ "trigger-before-download.trigger-timeout", NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },        /* 4 */
		{ NULL,                        NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};
	
	if (!p) return HANDLER_ERROR;
	
	p->config_storage = malloc(srv->config_context->used * sizeof(specific_config *));
	
	for (i = 0; i < srv->config_context->used; i++) {
		plugin_config *s;
		const char *errptr;
		int erroff;
		
		s = malloc(sizeof(plugin_config));
		s->db_filename    = buffer_init();
		s->download_url    = buffer_init();
		s->trigger_url    = buffer_init();
		s->deny_url    = buffer_init();
		
		cv[0].destination = s->db_filename;
		cv[1].destination = s->trigger_url;
		cv[2].destination = s->download_url;
		cv[3].destination = s->deny_url;
		cv[4].destination = &(s->trigger_timeout);
		
		p->config_storage[i] = s;
	
		if (0 != config_insert_values_global(srv, ((data_config *)srv->config_context->data[i])->value, cv)) {
			return HANDLER_ERROR;
		}
		
		if (!buffer_is_empty(s->db_filename)) {
			if (NULL == (s->db = gdbm_open(s->db_filename->ptr, 4096, GDBM_WRCREAT | GDBM_NOLOCK, S_IRUSR | S_IWUSR, 0))) {
				return HANDLER_ERROR;
			}
		}
		
		if (!buffer_is_empty(s->download_url)) {
			if (NULL == (s->download_regex = pcre_compile(s->download_url->ptr,
								      0, &errptr, &erroff, NULL))) {
				
				return HANDLER_ERROR;
			}
		}
		
		if (!buffer_is_empty(s->trigger_url)) {
			if (NULL == (s->trigger_regex = pcre_compile(s->trigger_url->ptr,
								     0, &errptr, &erroff, NULL))) {
				
				return HANDLER_ERROR;
			}
		}
	}
	
	return HANDLER_GO_ON;
}

#define PATCH(x) \
	p->conf.x = s->x;
static int mod_trigger_b4_dl_patch_connection(server *srv, connection *con, plugin_data *p, const char *stage, size_t stage_len) {
	size_t i, j;
	
	/* skip the first, the global context */
	for (i = 1; i < srv->config_context->used; i++) {
		data_config *dc = (data_config *)srv->config_context->data[i];
		plugin_config *s = p->config_storage[i];
		
		/* not our stage */
		if (!buffer_is_equal_string(dc->comp_key, stage, stage_len)) continue;
		
		/* condition didn't match */
		if (!config_check_cond(srv, con, dc)) continue;
		
		/* merge config */
		for (j = 0; j < dc->value->used; j++) {
			data_unset *du = dc->value->data[j];
			
			if (buffer_is_equal_string(du->key, CONST_STR_LEN("trigger-before-download.download-url"))) {
				PATCH(download_regex);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("trigger-before-download.trigger-url"))) {
				PATCH(trigger_regex);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("trigger-before-download.db-filename"))) {
				PATCH(db);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("trigger-before-download.trigger-timeout"))) {
				PATCH(trigger_timeout);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("trigger-before-download.deny_url"))) {
				PATCH(deny_url);
			}
		}
	}
	
	return 0;
}

static int mod_trigger_b4_dl_setup_connection(server *srv, connection *con, plugin_data *p) {
	plugin_config *s = p->config_storage[0];
	UNUSED(srv);
	UNUSED(con);
		
	PATCH(db);
	PATCH(download_regex);
	PATCH(trigger_regex);
	PATCH(trigger_timeout);
	PATCH(deny_url);
	
	return 0;
}
#undef PATCH

URIHANDLER_FUNC(mod_trigger_b4_dl_uri_handler) {
#ifdef HAVE_PCRE_H
	plugin_data *p = p_d;
	size_t i;
	const char *remote_ip;
	data_string *ds;
	
	int n;
# define N 10
	int ovec[N * 3];
	
	if (con->uri.path->used == 0) return HANDLER_GO_ON;
	
	mod_trigger_b4_dl_setup_connection(srv, con, p);
	for (i = 0; i < srv->config_patches->used; i++) {
		buffer *patch = srv->config_patches->ptr[i];
		
		mod_trigger_b4_dl_patch_connection(srv, con, p, CONST_BUF_LEN(patch));
	}
	
	if (!p->conf.trigger_regex || !p->conf.download_regex || !p->conf.db) return HANDLER_GO_ON;
	
	if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "X-Forwarded-For"))) {
		/* X-Forwarded-For contains the ip behind the proxy */
		
		remote_ip = ds->value->ptr;
	} else {
		remote_ip = inet_ntop_cache_get_ip(srv, &(con->dst_addr));
	}
	
	/* check if URL is a trigger -> insert IP into DB */
	if ((n = pcre_exec(p->conf.trigger_regex, NULL, con->uri.path->ptr, con->uri.path->used - 1, 0, 0, ovec, 3 * N)) < 0) {
		if (n != PCRE_ERROR_NOMATCH) {
			log_error_write(srv, __FILE__, __LINE__, "sd",
					"execution error while matching: ", n);
			
			return HANDLER_ERROR;
		}
	} else {
		/* the trigger matched */
		datum key, val;
		
		key.dptr = (char *)remote_ip;
		key.dsize = strlen(remote_ip);
		
		val.dptr = (char *)&(srv->cur_ts);
		val.dsize = sizeof(srv->cur_ts);
		
		if (0 != gdbm_store(p->conf.db, key, val, GDBM_REPLACE)) {
			log_error_write(srv, __FILE__, __LINE__, "s",
					"insert failed");
		}
	}
		
	/* check if URL is a download -> check IP in DB, update timestamp */
	if ((n = pcre_exec(p->conf.download_regex, NULL, con->uri.path->ptr, con->uri.path->used - 1, 0, 0, ovec, 3 * N)) < 0) {
		if (n != PCRE_ERROR_NOMATCH) {
			log_error_write(srv, __FILE__, __LINE__, "sd",
					"execution error while matching: ", n);
			return HANDLER_ERROR;
		}
	} else {
		/* the download uri matched */
		datum key, val;
		time_t last_hit;
		
		key.dptr = (char *)remote_ip;
		key.dsize = strlen(remote_ip);
		
		val = gdbm_fetch(p->conf.db, key);
		
		if (val.dptr == NULL) {
			/* not found, redirect */
			
			response_header_insert(srv, con, CONST_STR_LEN("Location"), CONST_BUF_LEN(p->conf.deny_url));
			
			con->http_status = 307;
			
			return HANDLER_FINISHED;
		}
		
		last_hit = *(time_t *)(val.dptr);
		
		free(val.dptr);
		
		if (srv->cur_ts - last_hit > p->conf.trigger_timeout) {
			/* found, but timeout, redirect */
			
			response_header_insert(srv, con, CONST_STR_LEN("Location"), CONST_BUF_LEN(p->conf.deny_url));
			con->http_status = 307;
			
			if (0 != gdbm_delete(p->conf.db, key)) {
				log_error_write(srv, __FILE__, __LINE__, "s",
						"delete failed");
			}
			
			return HANDLER_FINISHED;
		}
		
		val.dptr = (char *)&(srv->cur_ts);
		val.dsize = sizeof(srv->cur_ts);
		
		if (0 != gdbm_store(p->conf.db, key, val, GDBM_REPLACE)) {
			log_error_write(srv, __FILE__, __LINE__, "s",
					"insert failed");
		}
	}
	
#else
	UNUSED(srv);
	UNUSED(con);
	UNUSED(p_data);
#endif
	
	return HANDLER_GO_ON;
}

/* this function is called at dlopen() time and inits the callbacks */

int mod_trigger_b4_dl_plugin_init(plugin *p) {
	p->version     = LIGHTTPD_VERSION_ID;
	p->name        = buffer_init_string("trigger_b4_dl");
	
	p->init        = mod_trigger_b4_dl_init;
	p->handle_uri_clean  = mod_trigger_b4_dl_uri_handler;
	p->set_defaults  = mod_trigger_b4_dl_set_defaults;
	p->cleanup     = mod_trigger_b4_dl_free;
	
	p->data        = NULL;
	
	return 0;
}
