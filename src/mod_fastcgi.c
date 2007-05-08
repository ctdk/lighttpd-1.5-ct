#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <signal.h>

#include "buffer.h"
#include "server.h"
#include "keyvalue.h"
#include "log.h"

#include "fdevent.h"
#include "connections.h"
#include "response.h"
#include "joblist.h"
#include "status_counter.h"

#include "plugin.h"

#include "inet_ntop_cache.h"
#include "stat_cache.h"

#ifdef HAVE_FASTCGI_FASTCGI_H
#include <fastcgi/fastcgi.h>
#else
#ifdef HAVE_FASTCGI_H
#include <fastcgi.h>
#else
#include "fastcgi.h"
#endif
#endif /* HAVE_FASTCGI_FASTCGI_H */
#include <stdio.h>

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

#include "sys-socket.h"
#include "sys-files.h"
#include "sys-strings.h"
#include "sys-process.h"

#include "http_resp.h"

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX 108
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

typedef struct {
	int debug;
} plugin_config;

/* generic plugin data, shared between all connections */
typedef struct {
	PLUGIN_DATA;

	plugin_config **config_storage;

	plugin_config conf; /* this is only used as long as no handler_ctx is setup */
} plugin_data;

INIT_FUNC(mod_fastcgi_init) {
	plugin_data *p;

	UNUSED(srv);

	p = calloc(1, sizeof(*p));

	return p;
}

FREE_FUNC(mod_fastcgi_free) {
	plugin_data *p = p_d;

	UNUSED(srv);

	if (p->config_storage) {
		size_t i;
		for (i = 0; i < srv->config_context->used; i++) {
			plugin_config *s = p->config_storage[i];

			if (!s) continue;

			free(s);
		}
		free(p->config_storage);
	}

	free(p);

	return HANDLER_GO_ON;
}

SETDEFAULTS_FUNC(mod_fastcgi_set_defaults) {
	plugin_data *p = p_d;
	size_t i = 0;

	config_values_t cv[] = {
		{ "fastcgi.server",              "it is replaced by mod_proxy_core and mod_proxy_backend_fastcgi", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_CONNECTION },       /* 0 */
		{ "fastcgi.debug",               "it is replaced by mod_proxy_core and mod_proxy_backend_fastcgi", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_CONNECTION },       /* 1 */
		{ "fastcgi.map-extensions",      "it is replaced by mod_proxy_core and mod_proxy_backend_fastcgi", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_CONNECTION },       /* 2 */
		{ NULL,                          NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};

	p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));

	for (i = 0; i < srv->config_context->used; i++) {
		plugin_config *s = NULL;
		array *ca;

		p->config_storage[i] = s;
		ca = ((data_config *)srv->config_context->data[i])->value;

		if (0 != config_insert_values_global(srv, ca, cv)) {
			return HANDLER_ERROR;
		}
	}

	return HANDLER_GO_ON;
}

int mod_fastcgi_plugin_init(plugin *p) {
	p->version      = LIGHTTPD_VERSION_ID;
	p->name         = buffer_init_string("fastcgi");

	p->init         = mod_fastcgi_init;
	p->cleanup      = mod_fastcgi_free;
	p->set_defaults = mod_fastcgi_set_defaults;

	p->data         = NULL;

	return 0;
}
