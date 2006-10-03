#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "plugin.h"
#include "log.h"

#include "stat_cache.h"

#include "mod_sql_vhost_core.h"

#define plugin_data mod_sql_vhost_core_plugin_data
#define plugin_config mod_sql_vhost_core_plugin_config

/* init the plugin data */
INIT_FUNC(mod_sql_vhost_core_init) {
	plugin_data *p;

	p = calloc(1, sizeof(*p));

	p->docroot = buffer_init();
	p->host = buffer_init();

	return p;
}

/* cleanup the plugin data */
SERVER_FUNC(mod_sql_vhost_core_cleanup) {
	plugin_data *p = p_d;

	UNUSED(srv);

	if (!p) return HANDLER_GO_ON;

	if (p->config_storage) {
		size_t i;
		for (i = 0; i < srv->config_context->used; i++) {
			plugin_config *s = p->config_storage[i];

			if (!s) continue;

			buffer_free(s->db);
			buffer_free(s->user);
			buffer_free(s->pass);
			buffer_free(s->sock);
			buffer_free(s->backend);
			buffer_free(s->hostname);
			buffer_free(s->select_vhost);

			free(s);
		}
		free(p->config_storage);
	}
	buffer_free(p->docroot);
	buffer_free(p->host);

	free(p);

	return HANDLER_GO_ON;
}

/* set configuration values */
SERVER_FUNC(mod_sql_vhost_core_set_defaults) {
	plugin_data *p = p_d;

	size_t i = 0;

	config_values_t cv[] = {
		{ "sql-vhost.db",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 0 * e.g. vhost */
		{ "sql-vhost.user",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 1 * lighty */
		{ "sql-vhost.pass",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 2 * secrect */
		{ "sql-vhost.sock",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 3 * /tmp/mysql.sock */
		{ "sql-vhost.select-vhost", NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 4 * SELECT ... FROM hosts WHERE hostname = ? */
		{ "sql-vhost.hostname", NULL, T_CONFIG_STRING,  T_CONFIG_SCOPE_SERVER }, /* 5 * 127.0.0.1 */
		{ "sql-vhost.port",     NULL, T_CONFIG_SHORT,   T_CONFIG_SCOPE_SERVER }, /* 6 * 3306 */
		{ "sql-vhost.backend",  NULL, T_CONFIG_STRING,  T_CONFIG_SCOPE_SERVER }, /* 7 * mysql */

		/* backward compat */
		{ "mysql-vhost.db",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 8 == 0 */
		{ "mysql-vhost.user",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 9 == 1 */
		{ "mysql-vhost.pass",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 10 == 2 */
		{ "mysql-vhost.sock",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 11 == 3 */
		{ "mysql-vhost.sql",	NULL, T_CONFIG_STRING, 	T_CONFIG_SCOPE_SERVER }, /* 12 == 4 */
		{ "mysql-vhost.hostname", NULL, T_CONFIG_STRING,T_CONFIG_SCOPE_SERVER }, /* 13 == 5 */
		{ "mysql-vhost.port",   NULL, T_CONFIG_SHORT,   T_CONFIG_SCOPE_SERVER }, /* 14 == 6 */

                { NULL,			NULL, T_CONFIG_UNSET,	T_CONFIG_SCOPE_UNSET }
        };

	p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));

	for (i = 0; i < srv->config_context->used; i++) {
		plugin_config *s;

		s = calloc(1, sizeof(plugin_config));
		s->db = buffer_init();
		s->user = buffer_init();
		s->pass = buffer_init();
		s->sock = buffer_init();
		s->hostname = buffer_init();
		s->backend = buffer_init();
		s->port   = 0;               /* default port for mysql */
		s->select_vhost = buffer_init();
		s->backend_data = NULL;

		cv[0].destination = s->db;
		cv[1].destination = s->user;
		cv[2].destination = s->pass;
		cv[3].destination = s->sock;
		cv[4].destination = s->select_vhost;
		cv[5].destination = s->hostname;
		cv[6].destination = &(s->port);
		cv[7].destination = s->backend;

		/* backend compat */
		cv[8].destination = cv[0].destination;
		cv[9].destination = cv[1].destination;
		cv[10].destination = cv[2].destination;
		cv[11].destination = cv[3].destination;
		cv[12].destination = cv[4].destination;
		cv[13].destination = cv[5].destination;
		cv[14].destination = cv[6].destination;

		p->config_storage[i] = s;

        	if (config_insert_values_global(srv,
			((data_config *)srv->config_context->data[i])->value,
			cv)) return HANDLER_ERROR;

		/* we only parse the config, the backend plugin will patch itself into the plugin-struct */
	}

        return HANDLER_GO_ON;
}

static int mod_sql_vhost_core_patch_connection(server *srv, connection *con, plugin_data *p) {
	size_t i;
	plugin_config *s = p->config_storage[0];

	PATCH_OPTION(backend_data);
	PATCH_OPTION(get_vhost);

	/* skip the first, the global context */
	for (i = 1; i < srv->config_context->used; i++) {
		data_config *dc = (data_config *)srv->config_context->data[i];
		s = p->config_storage[i];

		/* condition didn't match */
		if (!config_check_cond(srv, con, dc)) continue;

		if (s->backend_data) {
			PATCH_OPTION(backend_data);
			PATCH_OPTION(get_vhost);
		}
	}

	return 0;
}

/* handle document root request */
CONNECTION_FUNC(mod_sql_vhost_core_handle_docroot) {
	plugin_data *p = p_d;
	stat_cache_entry *sce;

	/* no host specified? */
	if (!con->uri.authority->used) return HANDLER_GO_ON;

	mod_sql_vhost_core_patch_connection(srv, con, p);

	/* do we have backend ? */
	if (!p->conf.get_vhost) return HANDLER_GO_ON;

	/* ask the backend for the data */
	if (0 != p->conf.get_vhost(srv, con, p->conf.backend_data, p->docroot, p->host)) {
		return HANDLER_GO_ON;
	}

	if (HANDLER_ERROR == stat_cache_get_entry(srv, con, p->docroot, &sce)) {
		log_error_write(srv, __FILE__, __LINE__, "sb", strerror(errno), p->docroot);
		return HANDLER_GO_ON;
	}
        if (!S_ISDIR(sce->st.st_mode)) {
		log_error_write(srv, __FILE__, __LINE__, "sb", "Not a directory", p->docroot);
		return HANDLER_GO_ON;
	}

	buffer_copy_string_buffer(con->server_name, p->host);
	buffer_copy_string_buffer(con->physical.doc_root, p->docroot);

	return HANDLER_GO_ON;
}

/* this function is called at dlopen() time and inits the callbacks */
int mod_sql_vhost_core_plugin_init(plugin *p) {
	p->version     = LIGHTTPD_VERSION_ID;
	p->name        			= buffer_init_string("mod_sql_vhost_core");

	p->init        			= mod_sql_vhost_core_init;
	p->cleanup     			= mod_sql_vhost_core_cleanup;

	p->set_defaults			= mod_sql_vhost_core_set_defaults;
	p->handle_docroot  		= mod_sql_vhost_core_handle_docroot;

	return 0;
}

