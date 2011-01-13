#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include "mod_mcpage.h"

#ifdef HAVE_MEMCACHED
/* A plugin for serving up content from memcached instead of going back to the
 * backend server to get it. Created using the convenient mod_skeleton stuff
 * provided by lighttpd.
 */

static handler_ctx * handler_ctx_init() {
	handler_ctx * hctx;

	hctx = calloc(1, sizeof(*hctx));
	hctx->outpg = buffer_init();
	hctx->content_type = buffer_init();
	hctx->expires = buffer_init();
	hctx->cache_control = buffer_init();
	hctx->done = 0;
	hctx->nocomp = 0;
	hctx->deflate = 0;

	return hctx;
}

static void handler_ctx_free(handler_ctx *hctx) {
	buffer_free(hctx->outpg);
	buffer_free(hctx->content_type);
	buffer_free(hctx->expires);
	buffer_free(hctx->cache_control);
	free(hctx);
}

/* init the plugin data */
INIT_FUNC(mod_mcpage_init) {
	plugin_data *p;
	if(srv){ ; /* shut up compiler */ }

	p = calloc(1, sizeof(*p));

	p->match_buf = buffer_init();

	return p;
}

/* detroy the plugin data */
FREE_FUNC(mod_mcpage_free) {
	plugin_data *p = p_d;

	UNUSED(srv);

	if (!p) return HANDLER_GO_ON;

	if (p->config_storage) {
		size_t i;

		for (i = 0; i < srv->config_context->used; i++) {
			plugin_config *s = p->config_storage[i];

			if (!s) continue;

			array_free(s->mc_hosts);
			/* Eeesh, was I not freeing this stuff before? */
			/* Although, did I need to free the buffers, or was
			 * that done automatically? */
			buffer_free(s->mc_namespace);
			buffer_free(s->mc_keyprefix);
			buffer_free(s->mc_hashing);
			buffer_free(s->mc_behavior);
			if(s->mc) memcached_free(s->mc); /* Have an if(s-mc) statement
						* on the mod_magnet version of
						* this stuff for some reason.
						* Not having it, though, seems
						* to cause a segfault if it's
						* not defined, so let's keep it
						* after all.
						*/
			if(s->lmc) memcached_free(s->lmc);
			free(s);
		}
		free(p->config_storage);
	}

	buffer_free(p->match_buf);

	free(p);

	return HANDLER_GO_ON;
}

URIHANDLER_FUNC(mod_mcpage_con_reset){
	plugin_data *p = p_d;
	UNUSED(srv);
	if(con->plugin_ctx[p->id]){
		handler_ctx_free(con->plugin_ctx[p->id]);
		con->plugin_ctx[p->id] = NULL;
		}
	return HANDLER_GO_ON;
	}

/* handle plugin config and check values */

SETDEFAULTS_FUNC(mod_mcpage_set_defaults) {
	plugin_data *p = p_d;
	size_t i = 0;

/*  
 MCPAGE_CONFIG_MEMCACHED_ENABLE          "mcpage.enable"
 MCPAGE_CONFIG_MEMCACHED_HOSTS           "mcpage.memcached-hosts"
 MCPAGE_CONFIG_MEMCACHED_NS              "mcpage.memcached-namespace"
 MCPAGE_CONFIG_MEMCACHED_KEYPREFIX       "mcpage.memcached-keyprefix"
 MCPAGE_CONFIG_MEMCACHED_HASHING         "mcpage.memcached-hashing"
 MCPAGE_CONFIG_MEMCACHED_BEHAVIOR        "mcpage.memcached-behavior"
 MCPAGE_CONFIG_MEMCACHED_EXPIRE          "mcpage.memcached-expire"
 MCPAGE_CONFIG_MEMCACHED_MINSIZE         "mcpage.memcached-minsize"
 MCPAGE_CONFIG_DEBUG                     "mcpage.debug"
 MCPAGE_CONFIG_MEMCACHED_DEBUG           "mcpage.memcached-debug"
 MCPAGE_CONFIG_ZLIB_DEBUG                "mcpage.zlib-debug"
 MCPAGE_CONFIG_MEMCACHED_NOBLOCK         "mcpage.memcached-noblock"
 MCPAGE_CONFIG_MEMCACHED_PUT             "mcpage.memcached-put"
 MCPAGE_CONFIG_DEBUG_VERBOSE             "mcpage.debug-verbose"
 MCPAGE_CONFIG_LOCALMC_ENABLE		 "mcpage.localmc-enable"
 MCPAGE_CONFIG_LOCALMC_EXPIRE            "mcpage.localmc-expire"
 MCPAGE_CONFIG_LOCALMC_ADDR              "mcpage.localmc-addr"
 MCPAGE_CONFIG_LOCALMC_SOCKET            "mcpage.localmc-socket"
 MCPAGE_CONFIG_MEMCACHED_BINARY		 "mcpage.memcached-binary"
 MCPAGE_CONFIG_MD5                       "mcpage.md5"
 MCPAGE_CONFIG_ANNOUNCE                  "mcpage.announce"
 */

	config_values_t cv[] = {
		{ MCPAGE_CONFIG_MEMCACHED_ENABLE,             NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },       	/* 0 */
		 { MCPAGE_CONFIG_MEMCACHED_HOSTS, NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },			/* 1 */
		{ MCPAGE_CONFIG_MEMCACHED_NS, NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },			/* 2 */
		{ MCPAGE_CONFIG_MEMCACHED_KEYPREFIX, NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },		/* 3 */
		{ MCPAGE_CONFIG_MEMCACHED_HASHING, NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },			/* 4 */
		{ MCPAGE_CONFIG_MEMCACHED_BEHAVIOR, NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },			/* 5 */
		{ MCPAGE_CONFIG_DEBUG,             NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },			/* 6 */
		{ MCPAGE_CONFIG_MEMCACHED_DEBUG,             NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },	/* 7 */
		{ MCPAGE_CONFIG_ZLIB_DEBUG,             NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 8 */
		{ MCPAGE_CONFIG_MEMCACHED_EXPIRE,	NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },		/* 9 */
		{ MCPAGE_CONFIG_MEMCACHED_MINSIZE,      NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },		/* 10 */
		{ MCPAGE_CONFIG_MEMCACHED_NOBLOCK,      NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 11 */
		{ MCPAGE_CONFIG_MEMCACHED_PUT,		NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 12 */
		{ MCPAGE_CONFIG_DEBUG_VERBOSE,		NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 13 */
		{ MCPAGE_CONFIG_LOCALMC_ENABLE,         NULL, T_CONFIG_BOOLEAN,
T_CONFIG_SCOPE_CONNECTION },            /* 14 */
		{ MCPAGE_CONFIG_LOCALMC_EXPIRE,         NULL, T_CONFIG_SHORT,
T_CONFIG_SCOPE_CONNECTION },		/* 15 */
		{ MCPAGE_CONFIG_LOCALMC_ADDR,         NULL, T_CONFIG_STRING,
T_CONFIG_SCOPE_CONNECTION },		/* 16 */
		{ MCPAGE_CONFIG_LOCALMC_SOCKET,         NULL, T_CONFIG_STRING,
T_CONFIG_SCOPE_CONNECTION },		/* 17 */
		{ MCPAGE_CONFIG_MEMCACHED_BINARY,	NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 18 */
		{ MCPAGE_CONFIG_MD5,			NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 19 */
		{ MCPAGE_CONFIG_ANNOUNCE, 		 NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION },		/* 20 */
		{ NULL,                         NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};

	if (!p) return HANDLER_ERROR;

	p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));

	for (i = 0; i < srv->config_context->used; i++) {
		plugin_config *s;

		s = calloc(1, sizeof(plugin_config));
		s->enable	= 0;
		s->mc_hosts	= array_init();
		s->mc_namespace = buffer_init();
		s->mc_keyprefix = buffer_init();
		s->mc_hashing	= buffer_init();
		s->mc_behavior	= buffer_init();
		s->debug	= 0;
		s->mc_debug	= 0;
		s->zlib_debug	= 0;
		s->mc_expire	= 0;
		s->mc_minsize 	= 0;
		s->mc_noblock 	= 0;
		s->mc_put	= 1;
		s->debug_verbose = 0;
		s->lmc_enable	= 0;
		s->lmc_expire	= 0;
		s->lmc_addr	= buffer_init();
		s->lmc_socket	= buffer_init();
		s->mc_binary	= 0;
		s->md5		= 0;
		s->announce	= 1;

		cv[0].destination = &(s->enable);
		cv[1].destination = s->mc_hosts;
		cv[2].destination = s->mc_namespace;
		cv[3].destination = s->mc_keyprefix;
		cv[4].destination = s->mc_hashing;
		cv[5].destination = s->mc_behavior;
		cv[6].destination = &(s->debug);
		cv[7].destination = &(s->mc_debug);
		cv[8].destination = &(s->zlib_debug);
		cv[9].destination = &(s->mc_expire);
		cv[10].destination = &(s->mc_minsize);
		cv[11].destination = &(s->mc_noblock);
		cv[12].destination = &(s->mc_put);
		cv[13].destination = &(s->debug_verbose);
		cv[14].destination = &(s->lmc_enable);
		cv[15].destination = &(s->lmc_expire);
		cv[16].destination = s->lmc_addr;
		cv[17].destination = s->lmc_socket;
		cv[18].destination = &(s->mc_binary);
		cv[19].destination = &(s->md5);
		cv[20].destination = &(s->announce);

		p->config_storage[i] = s;

		if (0 != config_insert_values_global(srv, ((data_config *)srv->config_context->data[i])->value, cv)) {
			return HANDLER_ERROR;
		}

		/* check minsize for a sane value */
		if(s->mc_minsize > MC_MAX_SIZE){
			log_error_write(srv, __FILE__, __LINE__, "sdsds", "Values for mcpage.memcached-minsize over", MC_MAX_SIZE, " KB are not particularly useful, because memcached won't store objects bigger than that anyway. If you've recompiled memcached to store objects over",  MC_MAX_SIZE, " KB, then set MC_MAX_SIZE in mod_mcpage.h and recompile lighttpd.");
			return HANDLER_ERROR;
			}

		if (s->mc_hosts->used){
			/* Set memcached connections */
			size_t k;
			memcached_return rc;
			if(!s->mc){
				s->mc = memcached_create(NULL);
				/* The default hashing that libmemcached uses
				 * is not the same as what Cache::Memcached::-
				 * -Fast and Cache::Memcached use. To use the
				 * default that those modules use, set the
				 * mcpage.memcached-hashing option to CRC.
				 */
				uint64_t set;
				set = MEMCACHED_HASH_DEFAULT;
				/* Set hashing used */
				if(s->mc_hashing->used){
					if(!strcasecmp(s->mc_hashing->ptr, 								"CRC"))
						set = MEMCACHED_HASH_CRC;
					else if(!strcasecmp(s->mc_hashing->ptr,
							"FNV1_64"))
						set = MEMCACHED_HASH_FNV1_64;
					else if(!strcasecmp(s->mc_hashing->ptr, 
							"FNV1A_64"))
                                        	set = MEMCACHED_HASH_FNV1A_64;
					else if(!strcasecmp(s->mc_hashing->ptr, 
							"FNV1_32"))
                                        	set = MEMCACHED_HASH_FNV1_32;
					else if(!strcasecmp(s->mc_hashing->ptr, 
							"FNV1A_32"))
                                        	set = MEMCACHED_HASH_FNV1A_32;
					else {
						log_error_write(srv, __FILE__, __LINE__, "ss", "Hashing type not recognized: ", s->mc_hashing->ptr);
						return HANDLER_ERROR;
						}

					}
				rc = memcached_behavior_set(s->mc, MEMCACHED_BEHAVIOR_HASH, set);
				if (rc != MEMCACHED_SUCCESS){
					log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting memcached hashing behavior: ", memcached_strerror(s->mc, rc));
					return HANDLER_ERROR;
					}
				/* Set distribution behavior */
				/* Modula is the default */
				set = MEMCACHED_DISTRIBUTION_MODULA;
				if (s->mc_behavior->used){
					if(!strcasecmp(s->mc_behavior->ptr, 
						"MODULA"))
						set = MEMCACHED_DISTRIBUTION_MODULA;
					else if(!strcasecmp(s->mc_behavior->ptr,
						"CONSISTENT") ||
						!strcasecmp(s->mc_behavior->ptr,
						"KETAMA"))
						set = MEMCACHED_DISTRIBUTION_CONSISTENT;
					else {
						log_error_write(srv, __FILE__, __LINE__, "ss", "Distribution behavior type not recognized: ", s->mc_behavior->ptr);
						return HANDLER_ERROR;
						}
					}
				rc = memcached_behavior_set(s->mc, MEMCACHED_BEHAVIOR_DISTRIBUTION, set);
				if (rc != MEMCACHED_SUCCESS){
					log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting memcached distribution behavior: ", memcached_strerror(s->mc, rc));
					return HANDLER_ERROR;
					}
				if (s->mc_noblock){
					rc = memcached_behavior_set(s->mc, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
					if (rc != MEMCACHED_SUCCESS){
						log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting memcached nonblocking I/O: ", memcached_strerror(s->mc, rc));
						}
					rc = memcached_behavior_set(s->mc, MEMCACHED_BEHAVIOR_RCV_TIMEOUT, MC_SEND_TIMEOUT);
					if (rc != MEMCACHED_SUCCESS){
						log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting memcached send timeout: ", memcached_strerror(s->mc, rc));
						}
					}
				/* Are we using the binary protocol? */
				if (s->mc_binary) {
					rc = memcached_behavior_set(s->mc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
					if (rc != MEMCACHED_SUCCESS){
                                       		log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting memcached binary protocol: ", memcached_strerror(s->mc, rc));
                                        	return HANDLER_ERROR;
                                        	}
					}
				}
			for (k = 0; k < s->mc_hosts->used; k++){
				data_string *ds = (data_string *)s->mc_hosts->data[k];
				char *mctmp;
				/* gotta split IP and port. Hrmph. */
				/* to be on the safe side... */
				mctmp = malloc(ds->value->used);
				strcpy(mctmp, ds->value->ptr);
				char *ip, *portstr;
				char *sep = ":";
				int port;
				ip = strtok(mctmp, sep);
				portstr = strtok(NULL, sep);
				if (portstr == NULL)
					portstr = "11211";
				port = atoi(portstr);
				rc = memcached_server_add(s->mc, ip, port);
				if (p->conf.debug || p->conf.mc_debug){
					const char *mcstrerr = memcached_strerror(s->mc, rc);
					log_error_write(srv, __FILE__, __LINE__, "sssd", "Server add memcached_return was ", mcstrerr, " -- return code was: ", rc);
					log_error_write(srv, __FILE__, __LINE__, "sssd", "Server raw addy, hostname/ip and port were ", ds->value->ptr, ip, port);
					}

				free(mctmp);
				/* check for errors */
				if(rc != MEMCACHED_SUCCESS){
					log_error_write(srv, __FILE__, __LINE__, "sb",
						"connection to host failed:",
						ds->value);
					log_error_write(srv, __FILE__, __LINE__, "sssd", "Server add memcached_return was ", memcached_strerror(s->mc, rc), " -- return code was: ", rc);
					return HANDLER_ERROR;
					}
				}
			}
		/* local short-lived memcached cache.
		 * Someday, a supple shared mem hash would be sweet, but for
		 * now we'll let memcached deal with all the housekeeping.
		 */
		if (s->lmc_enable){
			memcached_return rc;
			if (!s->lmc){
				s->lmc = memcached_create(NULL);
				/* we'll only have the one local memcached, so
				 * just use the defaults, don't fuck with the
				 * hashing algorithms. */
				/* Also, may as well enable noblock if we use
				 * it with the rest of memcached */
				if (s->mc_noblock){
					rc = memcached_behavior_set(s->lmc, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
					if (rc != MEMCACHED_SUCCESS)
						log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting local memcached nonblocking I/O: ", memcached_strerror(s->lmc, rc));
					rc = memcached_behavior_set(s->lmc, MEMCACHED_BEHAVIOR_RCV_TIMEOUT, MC_SEND_TIMEOUT);
                                        if (rc != MEMCACHED_SUCCESS)
                                                log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting local memcached send timeout: ", memcached_strerror(s->lmc, rc));
					}
				/* Are we listening on the default address, a
				 * socket, or a specified address? 
				 */
				if (s->lmc_addr->used){
					char *lmctmp, *ip, *portstr;
					char *sep = ":";
					int port;
					lmctmp = malloc(s->lmc_addr->used);
					strcpy(lmctmp, s->lmc_addr->ptr);
					ip = strtok(lmctmp, sep);
					portstr = strtok(NULL, sep);
					if (portstr == NULL)
						portstr = "11211";
					port = atoi(portstr);
					rc = memcached_server_add(s->lmc, ip, port);
					free(lmctmp);
					}
				else if (s->lmc_socket->used){
					rc = memcached_server_add_unix_socket(s->lmc, s->lmc_socket->ptr);
					}
				else { /* The sensible default */
					rc = memcached_server_add(s->lmc, "127.0.0.1", 11211);
					}
				/* check for err */
				if (rc != MEMCACHED_SUCCESS) {
					log_error_write(srv, __FILE__, __LINE__, "s", "Connect to local cache failed!");
					return HANDLER_ERROR;
					}
				/* Using binary protocol? */ 
				if (s->mc_binary){
					rc = memcached_behavior_set(s->lmc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
                                	if (rc != MEMCACHED_SUCCESS){
                                        	log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting memcached binary protocol: ", memcached_strerror(s->lmc, rc));
                                        	return HANDLER_ERROR;
                                        	}
					}
				}
			}
	}

	return HANDLER_GO_ON;
}

#define PATCH(x) \
	p->conf.x = s->x;
static int mod_mcpage_patch_connection(server *srv, connection *con, plugin_data *p) {
	size_t i, j;
	plugin_config *s = p->config_storage[0];

	/* patch away */
	PATCH(enable);
	PATCH(mc_hosts);
	PATCH(mc_namespace);
	PATCH(mc_keyprefix);
	PATCH(mc);
	PATCH(lmc);
	PATCH(mc_hashing);
	PATCH(mc_behavior);
	PATCH(debug);
	PATCH(mc_debug);
	PATCH(zlib_debug);
	PATCH(mc_expire);
	PATCH(mc_minsize);
	PATCH(mc_noblock);
	PATCH(mc_put);
	PATCH(debug_verbose);
	PATCH(lmc_enable);
	PATCH(lmc_expire);
	PATCH(lmc_addr);
	PATCH(lmc_socket);
	PATCH(mc_binary);
	PATCH(md5);
	PATCH(announce);

	/* skip the first, the global context */
	for (i = 1; i < srv->config_context->used; i++) {
		data_config *dc = (data_config *)srv->config_context->data[i];
		s = p->config_storage[i];

		/* condition didn't match */
		if (!config_check_cond(srv, con, dc)) continue;

		/* merge config */
		for (j = 0; j < dc->value->used; j++) {
			data_unset *du = dc->value->data[j];

			if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_ENABLE))) {
				PATCH(enable);
			}
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_NS))) {
                                PATCH(mc_namespace);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_HOSTS))) {
                                PATCH(mc_hosts);
				/* patch this here? */
				PATCH(mc);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_KEYPREFIX))) {
                                PATCH(mc_keyprefix);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_HASHING))) {
                                PATCH(mc_hashing);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_BEHAVIOR))) {
                                PATCH(mc_behavior);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_DEBUG))) {
                                PATCH(debug);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_DEBUG))) {
                                PATCH(mc_debug);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_ZLIB_DEBUG))) {
                                PATCH(zlib_debug);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_EXPIRE))) {
                                PATCH(mc_expire);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_MINSIZE))) {
                                PATCH(mc_minsize);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_NOBLOCK))) {
                                PATCH(mc_noblock);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_PUT))) {
                                PATCH(mc_put);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_DEBUG_VERBOSE))) {
                                PATCH(debug_verbose);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_LOCALMC_ENABLE))) {
                                PATCH(lmc_enable);
				PATCH(lmc);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_LOCALMC_EXPIRE))) {
                                PATCH(lmc_expire);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_LOCALMC_ADDR))) {
                                PATCH(lmc_addr);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_LOCALMC_SOCKET))) {
                                PATCH(lmc_socket);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MEMCACHED_BINARY))){
				PATCH(mc_binary);
			}
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_MD5))){
                                PATCH(md5);
                        }
			else if (buffer_is_equal_string(du->key, CONST_STR_LEN(MCPAGE_CONFIG_ANNOUNCE))){
				PATCH(announce);
			}
		}
	}

	return 0;
}
#undef PATCH

URIHANDLER_FUNC(mod_mcpage_uri_handler) {
	plugin_data *p = p_d;
	char *retpage;
	char *content_type;
	char *send_page;
	char chunked;
	char nocomp;
	char exp;
	char ccon;
	char *cencoding = NULL;
	char *http_expires = NULL;
	char *http_cache_control = NULL;
	char *dpage = NULL; /* eh, be explicit */
	handler_ctx *hctx;
	mod_mcpage_patch_connection(srv, con, p);

	size_t value_length;
	size_t ct_length;
	size_t clen;
	size_t cenclen = 0;
	size_t elen = 0;
	size_t cclen = 0;
	size_t dval = 0;

        if(!p->conf.enable)
                return HANDLER_GO_ON;
	/* con->request.uri is the request we want */
	retpage = check_mc_page(srv, con, p, con->request.uri->ptr, &value_length);
	if (retpage != NULL){
		if(p->conf.debug || p->conf.mc_debug)
			log_error_write(srv, __FILE__, __LINE__, "ss", "Found memcached content for ", con->request.uri->ptr);
		/* extract the content type and page here */
		content_type = retpage;
		ct_length = strlen(content_type);
		chunked = *(retpage + ct_length + 2);
		nocomp = *(retpage + ct_length + 3);
		if(p->conf.debug || p->conf.mc_debug)
			log_error_write(srv, __FILE__, __LINE__, "sd", "nocomp is:", nocomp);
		if(nocomp){
			cencoding = retpage + ct_length + 4;
			cenclen = strlen(cencoding);
			}
		/* Extract expires & cache control too */
		/* Once working, clean up with better variable usage */
		exp = *(retpage + ct_length + 4 + ((cenclen) ? cenclen + 1 : 0));
		if(exp){
			http_expires = retpage + ct_length + 5 + ((cenclen) ? cenclen + 1 : 0);
			elen = strlen(http_expires);
			}

		ccon = *(retpage + ct_length + 5 + ((cenclen) ? cenclen + 1 : 0) + ((elen) ? elen + 1 : 0));
		if(ccon){
			http_cache_control = retpage + ct_length + 6 + ((cenclen) ? cenclen + 1 : 0) + ((elen) ? elen + 1 : 0);
			cclen = strlen(http_cache_control);
			}
		send_page = retpage + ct_length + 6 + ((cenclen) ? cenclen + 1 : 0) + ((elen) ? elen + 1 : 0) + ((cclen) ? cclen + 1 : 0); 

		if(nocomp){
			/* Check and see if the client can actually handle
			 * the compressed content. */
			data_string *dsk;
			data_string *dsu;
			dsu = (data_string *)array_get_element(con->request.headers, CONST_STR_LEN("User-Agent"));
			/* This ought to apply to most clients, really */
			/* don't let ab screw things up though */
			if ((NULL != (dsk = (data_string *)array_get_element(con->request.headers, CONST_STR_LEN("Accept-Encoding")))) || (dsu && (NULL != strstr(BUF_STR(dsu->value), "ApacheBench")))) {
				/* This should be a highly unusual circumstance,
				 * but apparently there's known issues with
				 * IE & 'deflate' - as in, IE claims that it can
				 * handle deflate, but doesn't actually handle
				 * it correctly. If we have content encoded with
				 * deflate and our user agent appears to be IE,
				 * it's probably safest to decompress it and
				 * let mod_deflate deal with it properly. This
				 * is along the lines of tossing it over the 
				 * fence and letting Arby's deal with it. 
				 *
				 * We also need to double check and make sure
				 * that the client is able to accept the 
				 * encoding. I have a hard time imagining where
				 * this would happen, but you never know. I 
				 * suppose someone could have a backend set up
				 * with compress enabled as a compression option
				 * and some weirdo browser would ask for 
				 * compress and only compress. Accept-Encoding:
				 * bzip2 might cause problems as well. I do not
				 * think that hardly anything supports bzip2 or
				 * compress though, so I'm not too concerned.
				 * This is more to prevent someone horking 
				 * this up accidentally or purposefully.
				 * If you don't send a user agent string *and*
				 * you incorrectly report that you can accept
				 * compressed output, cry me a river. */
				if ((dsk && cencoding && (NULL == strstr(BUF_STR(dsk->value), cencoding))) || (dsu && (NULL != strstr(BUF_STR(dsu->value), "MSIE")) && (dsk && (NULL != strstr(BUF_STR(dsk->value), "deflate"))))){
#ifdef USE_ZLIB
					MCLOGERR("Theoretically got IE and a request for deflate content");
					dpage = decomp(srv, p, &send_page, &dval);
#endif
					}
				else {
					MCLOGERR("Supposedly a request that can handle content encoded pages.");
					/* It's virtually certain that if we're
					 * here cencoding has been set, but 
					 * still it's good to check. Those seem
					 * like famous last words. */
					if(cencoding){
						response_header_overwrite(srv, con, CONST_STR_LEN("Content-Encoding"), cencoding, cenclen);
						response_header_insert(srv, con, CONST_STR_LEN("Vary"),
							 CONST_STR_LEN("Accept-Encoding"));
						}
					else {
						MCLOGERR("Very odd. nocomp was set but cencoding wasn't.");
						}
					}

				}
			else {
				/* Decompress it for the benefit of our less
				 * capable brethren */
#ifdef USE_ZLIB
				MCLOGERR("Decompressing, theoretically");
				dpage = decomp(srv, p, &send_page, &dval);
#endif
				}
			}

		if (exp && http_expires)
			response_header_overwrite(srv, con, CONST_STR_LEN("Expires"), http_expires, elen);
		if (ccon && http_cache_control)	
			response_header_overwrite(srv, con, CONST_STR_LEN("Cache-Control"), http_cache_control, cclen);
		if (dpage)
			send_page = dpage;

		if(p->conf.debug || p->conf.mc_debug){
			log_error_write(srv, __FILE__, __LINE__, "sd", "chunked is:", chunked);
			log_error_write(srv, __FILE__, __LINE__, "ss", "content type from retpage is:", content_type);
			log_error_write(srv, __FILE__, __LINE__, "sd", "strlen of send_page is:", strlen(send_page));
			log_error_write(srv, __FILE__, __LINE__, "ss", "send_page is:", send_page);
			}
		/* ? */
		clen = (dpage) ? dval : value_length - (ct_length + 7 + ((cenclen) ? cenclen + 1 : 0) + ((elen) ? elen + 1 : 0) + ((cclen) ? cclen + 1 : 0));
		/* Write content to chunkqueue, set content type, and HTTP
		* status, then return HANDLER_FINISHED. */
		chunkqueue_append_mem(con->send, send_page, clen + 1);

		response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"), content_type, ct_length);
		/* Perhaps? */
		/* if(chunked)
			response_header_overwrite(srv, con, CONST_STR_LEN("Transfer-Encoding"), CONST_STR_LEN("chunked"));  */

		if(p->conf.debug || p->conf.mc_debug || p->conf.announce)
			response_header_overwrite(srv, con, CONST_STR_LEN("X-Served-by-memcached"), CONST_STR_LEN("yes"));
		con->http_status = 200;
		con->send->is_closed = 1;
		con->send->bytes_in += chunkqueue_length(con->send); 
		con->response.content_length = clen; 
		if(p->conf.debug || p->conf.mc_debug)
			log_error_write(srv, __FILE__, __LINE__, "sd", "Content length supposedly is...", con->response.content_length);
		if (con->plugin_ctx[p->id])
                	hctx = con->plugin_ctx[p->id];
       		else {
                	hctx = handler_ctx_init();
                	con->plugin_ctx[p->id] = hctx;
                	}
		hctx->done = 1;
		/* can we free the retpage? */
		free(retpage);
		/* free dpage if it's not null */
		if (dpage != NULL)
			free(dpage);
		/* Perhaps this should be HANDLER_GO_ON? */
		return HANDLER_FINISHED;
		}

	/* not found */
	
	return HANDLER_GO_ON; 
}

/* subrequest handler. Right now it's just a stub to see what on earth
 * is available to work with.
 */

SUBREQUEST_FUNC(mod_mcpage_handle_response_header) {
	plugin_data *p = p_d;
	mod_mcpage_patch_connection(srv, con, p);
	handler_ctx *hctx;
	if(!p->conf.enable || !p->conf.mc_put)
		return HANDLER_GO_ON;
	if (con->plugin_ctx[p->id])
		hctx = con->plugin_ctx[p->id];
	else {
		hctx = handler_ctx_init();
		con->plugin_ctx[p->id] = hctx;
		}
	/* Really, right now we just want to see what we have */
	if(p->conf.debug || p->conf.mc_debug)
		MCLOGERR("In mod_mcpage_handle_response_header...");
	if(p->conf.debug && hctx->done){
		MCLOGERR("We think we're done, so we're skipping over this.");
		return HANDLER_GO_ON;
		}
	chunk *c;
	for(c = con->send->first; c; c = c->next){
		/* mark queue as touched */
		MCLOGERR("Touching chunk queue");
                if (c->touched){
                        MCLOGERR("Odd, chunk queue had been touched already");
			}
                else {
                        c->touched = 1;
                        MCLOGERR("cq touched");
                        }
		if(p->conf.debug_verbose)
			log_error_write(srv, __FILE__, __LINE__, "sd", "Cq touchedness is ", c->touched);

		switch(c->type){
		case MEM_CHUNK: {
			/* can we see here? */
                        data_string *dsr;
                        dsr = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Content-Encoding"));
                        if(dsr != NULL)
				hctx->deflate = 1;

			/* What chunk are we seeing up in here? */
			if(p->conf.debug_verbose)
                                log_error_write(srv, __FILE__, __LINE__, "ss", "Chunk queue in the subrequest handler is:\n", c->mem->ptr);

			buffer_append_string_len(hctx->outpg, c->mem->ptr, c->mem->used - 1);
			break;
			}
		case FILE_CHUNK: {
			MCLOGERR("Huh, we're in here with file chunk");
			int ifd;
			off_t offset;
			size_t toSend;
			if (-1 == (ifd = open(c->file.name->ptr, O_RDONLY))) {
                                log_error_write(srv, __FILE__, __LINE__, "ss", "open failed: ", strerror(errno));
                                break;
                                }
                        /* consider mmap once we see if this is working */
                        offset = c->file.start + c->offset;
                        toSend = c->file.length - c->offset;

                        buffer_prepare_copy(srv->tmp_buf, toSend);
                        lseek(ifd, offset, SEEK_SET);
                        if ((size_t)-1 == (toSend = read(ifd, srv->tmp_buf->ptr, toSend))) {
                                log_error_write(srv, __FILE__, __LINE__, "ss", "read: ", strerror(errno));
                                close(ifd);
                                break;
                                }
                        close(ifd);
			if(p->conf.debug_verbose)
                                log_error_write(srv, __FILE__, __LINE__, "ss", "File chunk queue in response header is:\n", srv->tmp_buf->ptr);
                        buffer_append_memory(hctx->outpg, srv->tmp_buf->ptr, toSend - 0);
			break;
			}
		default: {
			MCLOGERR("AIEYEYE! Unknown chunk type");
			return HANDLER_ERROR;
			break;
			}
			}
		}
	/* are we done somehow? */
	if (p->conf.debug){
		log_error_write(srv, __FILE__, __LINE__, "ss", "Do we think the cq is done already? ", (con->send->is_closed) ? "yes" : "no");
		}
	return HANDLER_GO_ON;
	}

SUBREQUEST_FUNC(mod_mcpage_handle_subrequest) {
	plugin_data *p = p_d;
	mod_mcpage_patch_connection(srv, con, p);
	handler_ctx *hctx;

	if(!p->conf.enable || !p->conf.mc_put)
                return HANDLER_GO_ON;
	/* OK... we need the handler_ctx stuff for this it seems. Weird that
         * we would for 1.5, but not 1.4. This'll make merging lots of fun I
         * imagine. */

	if (p->conf.debug)
                log_error_write(srv, __FILE__, __LINE__, "sd", "HTTP status is ", con->http_status);

        /* If http status seems to indicate an error, we don't want to store it,
         * so bail. NOTE: we may not want to store 3xx responses either. */
        if (con->http_status >= 400)
                return HANDLER_GO_ON;

        if (con->plugin_ctx[p->id])
                hctx = con->plugin_ctx[p->id];
        else {
                hctx = handler_ctx_init();
                con->plugin_ctx[p->id] = hctx;
                }
	if(!hctx->nocomp){
		data_string *dsr;
        	dsr = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Content-Encoding"));
		if(dsr != NULL){
			if(p->conf.debug)
				log_error_write(srv, __FILE__, __LINE__, "ss", "Will set content encoding to:", dsr->value->ptr);
			if(hctx->deflate)
				hctx->nocomp = 1;
			}
		else {
			if(p->conf.debug)
				MCLOGERR("Content encoding not set yet, will keep unset.");
			hctx->nocomp = -1; 
			}
		}
        if(hctx->done) {
		if(p->conf.debug)
			MCLOGERR("Already stored page in memcached, moving along.");
                return HANDLER_GO_ON;
		}
	if(p->conf.debug)
                log_error_write(srv, __FILE__, __LINE__, "ssssss", "Storing ", con->request.uri->ptr, " enabled? ", (p->conf.enable) ? "yes" : "no", "host: ", con->request.http_host->ptr);

	chunk *c;
	for(c = con->send->first; c; c = c->next){
		MCLOGERR("Checking chunk touch status");
		if (c->touched){
			MCLOGERR("This chunk got touched already. Continuing.");
			continue; 
			}
		else {
			c->touched = 1;
			MCLOGERR("Marked chunk as touched.");
			}
		switch(c->type){
		case MEM_CHUNK: {
			/* can we see here? */
			data_string *dsr;
                	dsr = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Content-Encoding"));
			/* if !hctx->deflate:
			 * NOW we're using mod_deflate 
			 * If we get here, then mod_deflate
			 * kicked in between the function calls
			 * and we should append the current
			 * chunk. */
			/* sigh.... */
			if(p->conf.debug_verbose)
				log_error_write(srv, __FILE__, __LINE__, "ss", "Chunk queue up here is:\n", c->mem->ptr);

			MCLOGERR("Wasn't touched, appending chunk.");
			buffer_append_string_len(hctx->outpg, c->mem->ptr, c->mem->used - 1);
			break;
			}
		case FILE_CHUNK: {
			/* stealing from network_write.c. That's OK, because
			 * the code above was too, I think. */
			int ifd;
			off_t offset;
			size_t toSend;
			if (-1 == (ifd = open(c->file.name->ptr, O_RDONLY))) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "open failed: ", strerror(errno));
				break;
				} 
			/* consider mmap once we see if this is working */
			offset = c->file.start + c->offset;
			toSend = c->file.length - c->offset;

			buffer_prepare_copy(srv->tmp_buf, toSend);
			lseek(ifd, offset, SEEK_SET);
			if ((size_t)-1 == (toSend = read(ifd, srv->tmp_buf->ptr, toSend))) { 
                                log_error_write(srv, __FILE__, __LINE__, "ss", "read: ", strerror(errno));
                                close(ifd);
				break;
				}
			close(ifd);
			if(p->conf.debug_verbose)
				log_error_write(srv, __FILE__, __LINE__, "ss", "File chunk queue here is:\n", srv->tmp_buf->ptr);
			buffer_append_memory(hctx->outpg, srv->tmp_buf->ptr, toSend - 0);
			break;
			}
		default: {
			MCLOGERR("Unknown chunk type!");
			return HANDLER_ERROR;
			}
			}	
		}
	if(con->send->is_closed == 1 && !hctx->done){ 
		data_string *ds;
        	ds = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Content-Type"));
		/* with expires and control-cache, I think they can be null and
		 * work OK. I am, however, very unsure of this. */
		data_string *de;
		data_string *dc;
		de = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Expires"));
		dc = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Cache-Control"));
		if(ds != NULL){
			if(p->conf.debug) {
				log_error_write(srv, __FILE__, __LINE__, "ss", "content type: ", ds->value->ptr);
				if (de != NULL)
				log_error_write(srv, __FILE__, __LINE__, "ss", "expires: ", de->value->ptr);
				if (dc != NULL)
				log_error_write(srv, __FILE__, __LINE__, "ss", "cache control: ", dc->value->ptr);
				}
			buffer_copy_string(hctx->content_type, ds->value->ptr);
			if (de != NULL && de->value->used)
				buffer_copy_string(hctx->expires, de->value->ptr);
			if (dc != NULL && dc->value->used)
				buffer_copy_string(hctx->cache_control, dc->value->ptr);

			/* test */
			if(p->conf.debug_verbose)
				log_error_write(srv, __FILE__, __LINE__, "ss", "Completed write queue is:\n", hctx->outpg->ptr);

			/* here's the fun part. Now we can get the page & 
			 *content type into memcached. Woo!
		 	 */
			if(p->conf.debug)
                                log_error_write(srv, __FILE__, __LINE__, "ss", "Getting ready to store content for ", con->request.uri->ptr);

			if(hctx->outpg->ptr == NULL){
				if(p->conf.debug)
					MCLOGERR("outpage was null, so we shouldn't be storing anything.");
				return HANDLER_GO_ON;
				}

			mcstore *mccontent = prep_for_memcached(srv, con, p, hctx);
			/* and store that bad motherfucker */
			store_mc_page(srv, con, p, mccontent);
			mcstore_free(mccontent);

			}
		hctx->done = 1;
		}

	/* just return HANDLER_GO_ON no matter what - we don't want to change
	 * anything here, just slurp in what's being returned for later.
	 */
	return HANDLER_GO_ON;
	}

/* this function is called at dlopen() time and inits the callbacks */

int mod_mcpage_plugin_init(plugin *p) {
	p->version     = LIGHTTPD_VERSION_ID;
	p->name        = buffer_init_string("mcpage");

	p->init        = mod_mcpage_init;
	p->handle_uri_clean  = mod_mcpage_uri_handler;
	/* have to be split up because of vagarities with mod_deflate */
	p->handle_response_header = mod_mcpage_handle_response_header;
	p->handle_filter_response_content = mod_mcpage_handle_subrequest; 
	p->set_defaults  = mod_mcpage_set_defaults;
	p->cleanup     = mod_mcpage_free;
	p->connection_reset = mod_mcpage_con_reset;

	p->data        = NULL;

	return 0;
}

/* Checks and sees if there's a page available in memcached for this request.
 * Returns a char pointer if there is, and NULL if not.
 */

char * check_mc_page(server *srv, connection *con, plugin_data *p, char *r, size_t *value_length){
	/* Namespace is what namespace you want the memcached keys to be in,
	 * the key prefix is what the URI is prefixed with to go along with
	 * the namespace to make the key. The memcached key is made like so:
	 * namespace + key prefix + uri 
	 */
	char *mckey;
	char *retpage;
	uint32_t flags;
	/* size_t value_length; */
	memcached_return rc;

	char *rkey;
	/* depending on how things are set, rkey might be just 'r', or it might
	 * be an md5 hash. */
	if(p->conf.md5)
		rkey = md5hash(r);
	else {
		rkey = malloc(strlen(r) + 1);
		strcpy(rkey, r);
		}

	if(p->conf.debug || p->conf.mc_debug)
		log_error_write(srv, __FILE__, __LINE__, "ssss", "Namespace? ", p->conf.mc_namespace->ptr, " Request URI: ", r);
        /* screw it, just set the namespace to the hostname if mc_namespace is
         * blank. */
        if (!p->conf.mc_namespace->used){
                buffer_copy_string_buffer(p->conf.mc_namespace, con->request.http_host);
                if (p->conf.debug || p->conf.mc_debug)
                                log_error_write(srv, __FILE__, __LINE__, "ss", "mc_namespace blank, set to ", p->conf.mc_namespace->ptr);
                }


	size_t mcksize = ((p->conf.mc_keyprefix->used) ? p->conf.mc_keyprefix->used - 1 : 0 ) + p->conf.mc_namespace->used - 1 + strlen(rkey) + 1;
	mckey = malloc(mcksize);
	/* sizeof, or strlen? */
	/* These may need fixing */
	strncpy(mckey, p->conf.mc_namespace->ptr, mcksize);
	if (p->conf.mc_keyprefix->used)
		strncat(mckey, p->conf.mc_keyprefix->ptr, mcksize);
	strncat(mckey, rkey, mcksize);

	/* If the local cache is enabled, look there first. If it isn't there,
	 * get it from the remote servers, and if it's there set it in the 
	 * local cache.
	 *
	 * Otherwise, default to the normal behavior and just get it from the
	 * remote memcached server(s).
	 */
	/* Let's try using mget to get the non-block getting */
	/* And that breaks about 10,000 different ways...
	 * I'm going to leave it in though, in hopes of making it work down
	 * the road.
	 */
	/* char *keys[] = { mckey };
	size_t lengths[] = { strlen(mckey) };
	char key[MEMCACHED_MAX_KEY];
	size_t klength;
	*/

	if (p->conf.lmc_enable){
		memcached_return orc;
		retpage = memcached_get(p->conf.lmc, mckey, strlen(mckey), &(*value_length), &flags, &orc);
		/*
		orc = memcached_mget(p->conf.lmc, keys, lengths, 1);
		if (orc != MEMCACHED_SUCCESS){
			free(mckey);
			free(rkey);
			return NULL;
			}
		retpage = memcached_fetch(p->conf.lmc, key, &klength, &(*value_length), &flags, &orc);
		memcached_fetch(p->conf.lmc, key, &klength, &(*value_length), &flags, &orc);
		*/
	
		if (retpage == NULL){
			retpage = memcached_get(p->conf.mc, mckey, strlen(mckey), &(*value_length), &flags, &rc);
			/*
			orc = memcached_mget(p->conf.mc, keys, lengths, 1);
			if (orc != MEMCACHED_SUCCESS){
                        	free(mckey);
                        	free(rkey);
                        	return NULL;
                        	}
			retpage = memcached_fetch(p->conf.mc, key, &klength, &(*value_length), &flags, &orc);
                	memcached_fetch(p->conf.mc, key, &klength, &(*value_length), &flags, &orc);
			*/
			
			if (retpage != NULL){
				unsigned short exp = (p->conf.lmc_expire) ? p->conf.lmc_expire : LMC_DEFAULT_EXPIRY;
				orc = memcached_set(p->conf.lmc, mckey, strlen(mckey), retpage, *value_length, exp, flags);
				if (orc != MEMCACHED_SUCCESS && orc != MEMCACHED_BUFFERED)
					if(p->conf.debug || p->conf.mc_debug)
						log_error_write(srv, __FILE__, __LINE__, "ss", "(local cache) Error setting key (data for local fetched from remote): ", memcached_strerror(p->conf.lmc, orc));

				}
			}
		}
	else {
		retpage = memcached_get(p->conf.mc, mckey, strlen(mckey), &(*value_length), &flags, &rc);
		/*
		rc = memcached_mget(p->conf.mc, keys, lengths, 1);
                if (rc != MEMCACHED_SUCCESS){
                	free(mckey);
                        free(rkey);
                        return NULL;
                        }
                retpage = memcached_fetch(p->conf.mc, key, &klength, &(*value_length), &flags, &rc);
                memcached_fetch(p->conf.mc, key, &klength, &(*value_length), &flags, &rc);
		*/
		}
	if(p->conf.debug || p->conf.mc_debug){
		const char *mcstrerr = memcached_strerror(p->conf.mc, rc);
		log_error_write(srv, __FILE__, __LINE__, "sssd",
 		"Memcached fetch in check_mc_page memcached_return was ", mcstrerr, " -- return code was: ", rc);
		log_error_write(srv, __FILE__, __LINE__, "ss", "mckey was ", mckey);
		}

	if (retpage == NULL){
		if(p->conf.debug || p->conf.mc_debug)
			log_error_write(srv, __FILE__, __LINE__, "sss", "No memcached content for ", r, mckey);
		/* This is actually OK. It just means nothing was there. Or,
		 * it might mean that there was some sort of error with
		 * decompressing the content returned, in which case we just
		 * bailed and assume nothing's there. */
		free(mckey);
		free(rkey);
		return NULL;
		}
	/* decompress if need be */
	free(mckey);
	free(rkey);
	dcmp_page(srv, &retpage, r, p, &(*value_length));
	/* And send it back */
	return retpage;
	}

/* return void here, perhaps, and just modify the pointer. We'll see if it works or not */
/* NOTE: May want to return an int instead, if we end up needing to examine
 * return values.
 */
/* Assuming it works, this function just checks to see if a page returned from
   memcached is compressed or not, and if it is, decompress it and swap the
   pointers. */
void dcmp_page(server *srv, char **chkpage, char *r, plugin_data *p, size_t *value_length){
#ifdef USE_ZLIB
	/* Fun with zlib structures */
	z_stream *zstr;
	int zerr, zr;
	unsigned int obsize, tsize, have;
	unsigned int ib = 0;
	zerr = zr = obsize = tsize = 0;
	unsigned char *in;
	unsigned char *out;
	unsigned char *outbuf = malloc(0);
	unsigned char *ob2;
	/* Are we even gzipped? */
	if (!memcmp(*chkpage,"\x1f\x8b",2)){

		if (p->conf.debug || p->conf.zlib_debug)
			log_error_write(srv,__FILE__,__LINE__, "ss", "Starting to examing gzipped content:", *chkpage, r);

		/* Lets set the zlib crap up then. */
		zstr = (z_stream *)malloc(sizeof(z_stream));
		zstr->zalloc = Z_NULL;
		zstr->zfree = Z_NULL;
		zstr->opaque = Z_NULL;
		zstr->next_in = Z_NULL;
		zstr->avail_in = 0;
		/* init buffers */
		in = malloc(Z_BUFSIZE);
		out = malloc(Z_BUFSIZE);
		zr = inflateInit2(zstr, GZWBITS);
		if (zr != Z_OK){

			if (p->conf.debug || p->conf.zlib_debug)
				log_error_write(srv, __FILE__, __LINE__, "ss", "inflateInit2 failed with: ", zstr->msg ? zstr->msg : "no message");

			/* Just bail out silently. We use the harmful goto
			 * to make sure everything's cleaned up. We may,
			 * however, take this out later depending on how this
			 * version works. 
			 */
			free((*chkpage)); /* make chkpage NULL, so it thinks
					   * nothing was returned. */
			*chkpage = NULL;
			goto considered_harmful;
			}
		do { /* begin first do/while */
			/* Read in string from memcached */
			zstr->avail_in = Z_BUFSIZE;
			memcpy(in, (*chkpage)+ib,Z_BUFSIZE);
			ib += Z_BUFSIZE;
			zstr->next_in = in;
			do { /* begin second do/while */
				zstr->avail_out = Z_BUFSIZE;
				zstr->next_out = out;
				zerr = inflate(zstr, Z_SYNC_FLUSH);
				/* checking for errors with code not only
				 * stolen from the zlib howto, but code I wrote
				 * using code stolen from the zlib code from 
				 * before. w00!
				 */
				switch (zerr) {
					case Z_NEED_DICT:
						zerr = Z_DATA_ERROR;
					case Z_DATA_ERROR:
					case Z_MEM_ERROR:
					case Z_STREAM_ERROR:
						if (p->conf.debug || p->conf.zlib_debug)
							log_error_write(srv, __FILE__, __LINE__, "ss", "inflate failed with: ", zstr->msg ? zstr->msg : "no message");
						goto considered_harmful;
					}
				have = Z_BUFSIZE - zstr->avail_out;
				tsize = obsize;
				obsize += have;
				ob2 = realloc(outbuf, obsize);
				if (ob2 == NULL){
					log_error_write(srv, __FILE__, __LINE__, "s", "reallocing outbuf failed!");
					if(tsize != 0)
						goto considered_harmful;
					}
				else {
					outbuf = ob2;
					}
				memmove(outbuf+tsize,out,have);
				} while (zstr->avail_out == 0); /* end second 
								* do/while */
			} while (zerr != Z_STREAM_END); /* end first do/while */

		considered_harmful:
		if(zerr == Z_STREAM_END){
			outbuf[zstr->total_out] = '\0';

			if (p->conf.debug || p->conf.zlib_debug)
				log_error_write(srv, __FILE__, __LINE__, "ss", "We served up gzipped content!", r);

			/* Do the pointer swapping here */
			/* this *should* be how you do it */
			free((*chkpage)); /* ? */
			*chkpage = (char *) outbuf;
			/* this should set value_length correctly */
			*value_length = zstr->total_out;
			}
		else {
			if (p->conf.debug || p->conf.zlib_debug)
				log_error_write(srv, __FILE__, __LINE__, "sssdsd", "Serving up gzipped content failed: ", r, "read in ", zstr->total_in, "wrote out ", zstr->total_out);
			/* set checkpage to NULL here too */
			free((*chkpage));
			*chkpage = NULL;
			}
		/* clean up */
		/* NOTE: Sometimes, once in a blue moon, inflateEnd here will
		 * segfault. I still haven't found out why, and it might be a
		 * situation where it's best that that happen, but I'll keep
		 * looking in the meantime.
		 */
		(void)inflateEnd(zstr);
		/* *should* we be freeing zstr? */
		free(zstr);
		free(in);
		free(out);
		}
	else {
		if (p->conf.debug || p->conf.zlib_debug)
			log_error_write(srv, __FILE__, __LINE__, "ss", "Theoretically not gzipped content: ", r);
		/* Shouldn't need to do anything, I don't think. */
		}

#else
	/* Shouldn't really need to do anything. */
#endif
	}

mcstore * mcstore_init(){
	mcstore *mccontent;
	mccontent = calloc(1, sizeof(*mccontent));
	mccontent->page = NULL;
	mccontent->uri = NULL;
	return mccontent;
	}

void mcstore_free(mcstore *mccontent){
	if(mccontent->page != NULL)
		free(mccontent->page);
	if(mccontent->uri != NULL)
		free(mccontent->uri);
	mccontent->page = NULL;
	mccontent->uri = NULL;
	free(mccontent);
	}

mcpre * mcpre_init(){
        mcpre *pagestore;
        pagestore = calloc(1, sizeof(*pagestore));
        pagestore->outpg = buffer_init(); /* we'll grow this as needed. */
	pagestore->content_type = buffer_init();
        return pagestore;
        }

void mcpre_free(mcpre *pagestore){
        buffer_free(pagestore->outpg);
	buffer_free(pagestore->content_type);
        free(pagestore);
        }

mcstore * prep_for_memcached(server *srv, connection *con, plugin_data *p, handler_ctx *pagestore){
	/* We may want plugin data for debugging too. */
	mcstore *mccontent = mcstore_init();
	char chunked = (con->response.transfer_encoding & HTTP_TRANSFER_ENCODING_CHUNKED) ? 1 : 0;
	char nocomp = 0;
	char bindat = 0;
	size_t cenclen = 0;

	/* hopefully this works? */
	data_string *ds = NULL; /* silence compiler warning */
	if (pagestore->nocomp == 1 && (NULL != (ds = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Content-Encoding"))))) {
		if(p->conf.debug || p->conf.mc_debug)
			log_error_write(srv, __FILE__, __LINE__, "ss", "It haz encoding:", ds->value->ptr);
		nocomp = 1;
		cenclen = ds->value->used;
		}
	/* Try looking at first hundred bytes or so of data? */
	int i;
	int k = (pagestore->outpg->used > MC_INS_LIMIT) ? MC_INS_LIMIT : pagestore->outpg->used;
	int ci = 0;
	for (i = 0; i < k; i++){
		if(pagestore->outpg->ptr[i] < 32 || pagestore->outpg->ptr[i] > 126)
			ci++;
		}
	bindat = (ci > MC_BYTE_PER) ? 1 : 0;

	if((p->conf.debug || p->conf.mc_debug) && !nocomp && !bindat)
		buffer_append_string(pagestore->outpg, "\n<!-- stored in memcached --!>\n"); 

	/* join the content-type, chunking, and page */
	if(p->conf.debug_verbose)
		log_error_write(srv, __FILE__, __LINE__, "ss", "content type in prep_for_memcached:", pagestore->content_type->ptr);
	mccontent->mclen = pagestore->content_type->used + pagestore->outpg->used - 1 + (sizeof(char) * 5) + cenclen + pagestore->expires->used + pagestore->cache_control->used;

	if(p->conf.debug || p->conf.mc_debug)
                log_error_write(srv, __FILE__, __LINE__, "sd", "page stored is ", pagestore->outpg->used);
	
	if(p->conf.debug || p->conf.mc_debug)
		log_error_write(srv, __FILE__, __LINE__, "sd", "mclen is ", mccontent->mclen);

	mccontent->page = malloc(mccontent->mclen * sizeof(char));
        mccontent->uri = malloc(con->request.uri->used * sizeof(char));
	strcpy(mccontent->uri, con->request.uri->ptr);

	/* Might be better to use memcpy rather than strcat. Hmmm. It might
	 * also require less jumping around randomly.
	 */

	memcpy(mccontent->page, pagestore->content_type->ptr, pagestore->content_type->used);
	*(mccontent->page + pagestore->content_type->used + 1) = chunked;
	*(mccontent->page + pagestore->content_type->used + 2) = nocomp; 

	if(nocomp)
		memcpy(mccontent->page + pagestore->content_type->used + 3, ds->value->ptr, ds->value->used);

	*(mccontent->page + pagestore->content_type->used + cenclen + 3) = (pagestore->expires->used) ? 1 : 0;

	if (pagestore->expires->used)
		memcpy(mccontent->page + pagestore->content_type->used + cenclen + 4, pagestore->expires->ptr, pagestore->expires->used);

	*(mccontent->page + pagestore->content_type->used + cenclen + 4 + pagestore->expires->used) = (pagestore->cache_control->used) ? 1 : 0;

	if (pagestore->cache_control->used)
		memcpy(mccontent->page + pagestore->content_type->used + cenclen + pagestore->expires->used + 5, pagestore->cache_control->ptr, pagestore->cache_control->used);

	memcpy(mccontent->page + pagestore->content_type->used + cenclen + 5 + pagestore->expires->used + pagestore->cache_control->used, pagestore->outpg->ptr, pagestore->outpg->used - 1);
	
	/* compress if need be */
	if(!nocomp && !bindat)
		compress_page(srv, con, p, &mccontent);

	/* and send it back */
	return mccontent;
	}

void compress_page(server *srv, connection *con, plugin_data *p, mcstore **mccontent){
#ifdef USE_ZLIB
	/* we have to store minsize in a buffer, so convert it to the size
	 * of bytes we need first. Hmph. */

	/* We don't need the con struct in here, but it's been necessary in the
	 * past, so we're still passing it in. This is to shut the compiler
	 * up. */
	if (con){ ; }

	size_t min_size = p->conf.mc_minsize * 1024; /* p->conf.mc_minsize has
							to be in KB, because
							there doesn't seem to
							be a way to allow
							ints in the config
							settings. However,
							values above 1MB aren't
							very useful anyway,
							since memcached won't
							store anything bigger
							than that by default. */

	/* TODO: add mimetype checks - borrow from mod_deflate */

	if (p->conf.mc_minsize && (*mccontent)->mclen > min_size){
		/* if it's long enough, set up the compression goodies */
		int zerr, zr;
		z_stream *zstr;
		unsigned int obsize, tsize, have, ib, togo;
		zerr = zr = obsize = tsize = ib = togo = 0;
		unsigned char *in;
		unsigned char *out;
		unsigned char *outbuf = malloc(0);
		unsigned char *ob2;
		int flush;
			int read_remaining = (*mccontent)->mclen;
			int zread;

			zstr = (z_stream *)malloc(sizeof(z_stream));
			zstr->zalloc = Z_NULL;
			zstr->zfree = Z_NULL;
			zstr->opaque = Z_NULL;
			zstr->next_in = Z_NULL;
			zstr->avail_in = 0;

			in = malloc(Z_BUFSIZE);
			out = malloc(Z_BUFSIZE);

			zr = deflateInit2(zstr, Z_DEFAULT_COMPRESSION, Z_DEFLATED, GZWBITS, GZ_COMP_LEV, Z_DEFAULT_STRATEGY);
			if (zr != Z_OK){
				if (p->conf.debug || p->conf.zlib_debug)
					log_error_write(srv, __FILE__, __LINE__, "ss", "deflateInit2 failed with: ", zstr->msg ? zstr->msg : "no message");
				goto is_a_bad_idea;
				}

			do { /* begin first do/while */
				if(read_remaining >= Z_BUFSIZE){
					read_remaining -= Z_BUFSIZE;
					zread = Z_BUFSIZE;
					flush = Z_NO_FLUSH;
					}
				else {
					zread = read_remaining;
					read_remaining = 0;
					flush = Z_FINISH;
					}
				zstr->avail_in = zread;
				memcpy(in, (*mccontent)->page + ib, zread);
				ib += Z_BUFSIZE;
			togo = (*mccontent)->mclen - ib;
			zstr->next_in = in;
			do {
				zstr->avail_out = Z_BUFSIZE;
				zstr->next_out = out;
				zerr = deflate(zstr, flush);
				/* stealing err code from dcmp_page, which
				 * itself is stolen from elsewhere. Yay!.
				 */
				switch (zerr) {
					case Z_STREAM_ERROR:
					case Z_BUF_ERROR:
						if (p->conf.debug || p->conf.zlib_debug)
                                                        log_error_write(srv, __FILE__, __LINE__, "ss", "deflate failed with: ", zstr->msg ? zstr->msg : "no message");
                                                goto is_a_bad_idea;
					}
				have = Z_BUFSIZE - zstr->avail_out;
				tsize = obsize;
				obsize += have;
				ob2 = realloc(outbuf, obsize);
				if (ob2 == NULL){
					log_error_write(srv, __FILE__, __LINE__, "s", "reallocing deflate outbuf failed!");
                                        if(tsize != 0)
                                                goto is_a_bad_idea;
					}
				else {
					/* fuuuck, I can't remember - will this
					 * cause Incredibly Bad Things? */
					/* free(outbuf); */
					outbuf = ob2;
					}
				memmove(outbuf+tsize,out,have);
				} while (zstr->avail_out == 0); /* end second
								* do/while */
			} while (flush != Z_FINISH);

		is_a_bad_idea:
		/* jump here to clean up decompression related stuff */
		if(zerr == Z_STREAM_END){ 
			/* here, at least, we don't have to worry about
			 * setting a null pointer. However, we *do* need to be
			 * careful about moving the gzipped content in. Hmm.
			 * outbuf might have gobbledegock in it, so we may need
			 * to memcpy stuff over. Hard to say.
			 */
			if (p->conf.debug || p->conf.zlib_debug)
                                log_error_write(srv, __FILE__, __LINE__, "ss", "compressed the content for", (*mccontent)->uri);
			/* Let's try *not* doing the memcpy stuff, and just see
			 * what happens. */
			free((*mccontent)->page);
			(*mccontent)->page = (char *) outbuf;
			/* I think this is correct */
			(*mccontent)->mclen = zstr->total_out;
			}
		else {
			if (p->conf.debug || p->conf.zlib_debug)
                                log_error_write(srv, __FILE__, __LINE__, "sssdsd", "gzipping content failed: ", (*mccontent)->uri, "read in ", zstr->total_in, "wrote out ", zstr->total_out);
			}

		/* clean up */
		(void)deflateEnd(zstr);
		free(zstr);
		free(in);
		free(out);
		}
	else {
		if (p->conf.debug || p->conf.zlib_debug)
			log_error_write(srv, __FILE__, __LINE__, "ss", "Theoretically didn't need to gzip this request - ", (*mccontent)->uri);
		}
#endif
	}

void store_mc_page(server *srv, connection *con, plugin_data *p, mcstore *mccontent){
        char *mckey;
        uint32_t flags = 0;
	time_t mcexpire = 0;
        memcached_return rc;

        if(p->conf.debug || p->conf.mc_debug)
                log_error_write(srv, __FILE__, __LINE__, "ssss", "Namespace? ", p->conf.mc_namespace->ptr, " Request URI: ", mccontent->uri);

	/* Do we have an expire time set? */
	if(p->conf.mc_expire != 0){
		mcexpire = time(NULL) + (p->conf.mc_expire * 60);
		}
	/* screw it, just set the namespace to the hostname if mc_namespace is
	 * blank. */
	char *rkey;
	if (p->conf.md5)
		rkey = md5hash(mccontent->uri);
	else {
		rkey = malloc(strlen(mccontent->uri) + 1);
		strcpy(rkey, mccontent->uri);
		}
	if (!p->conf.mc_namespace->used){
		buffer_copy_string_buffer(p->conf.mc_namespace, con->request.http_host);
                if (p->conf.debug || p->conf.mc_debug)
                                log_error_write(srv, __FILE__, __LINE__, "ss", "mc_namespace blank, set to ", p->conf.mc_namespace->ptr);
		}

        size_t mcksize = ((p->conf.mc_keyprefix->used) ? p->conf.mc_keyprefix->used - 1 : 0 ) + p->conf.mc_namespace->used - 1 + strlen(rkey) + 1;
        mckey = malloc(mcksize);
        strncpy(mckey, p->conf.mc_namespace->ptr, mcksize);
        if (p->conf.mc_keyprefix->used)
                strncat(mckey, p->conf.mc_keyprefix->ptr, mcksize);
        strncat(mckey, rkey, mcksize);

	/* set expiry later */
	/* Only store the value if it's less than the max memcached size */
	if(mccontent->mclen < (MC_MAX_SIZE * 1024)) {
		/* The local memcached cache */
		if (p->conf.lmc_enable) {
			unsigned short exp = (p->conf.lmc_expire) ? p->conf.lmc_expire : LMC_DEFAULT_EXPIRY;
			rc = memcached_set(p->conf.lmc, mckey, strlen(mckey), mccontent->page, mccontent->mclen, exp, flags);
			if(rc != MEMCACHED_SUCCESS && rc != MEMCACHED_BUFFERED)
				if(p->conf.debug || p->conf.mc_debug)
					log_error_write(srv, __FILE__, __LINE__, "ss", "(local cache) Error setting key: ", memcached_strerror(p->conf.lmc, rc));
			}
		rc = memcached_set(p->conf.mc, mckey, strlen(mckey), mccontent->page, mccontent->mclen, mcexpire, flags);
		/* if this goes wrong, it's *probably* just a network error, so
	 	* log it, and let life go on. */
		if(rc != MEMCACHED_SUCCESS && rc != MEMCACHED_BUFFERED)
			if(p->conf.debug || p->conf.mc_debug)
				log_error_write(srv, __FILE__, __LINE__, "ss", "Error setting key: ", memcached_strerror(p->conf.mc, rc));
		}
	if(p->conf.debug || p->conf.mc_debug)
			log_error_write(srv, __FILE__, __LINE__, "ss", "mckey was ", mckey);

	free(mckey);
	free(rkey);
	}

/* We might want to md5-hash urls for keys */

#define MD5LEN	33
char *md5hash(char *str){
	MD5_CTX c;
        unsigned char hash[16];
        char *hexhash;
	
	/* Don't you forget to free me */
	hexhash = malloc(MD5LEN);

        MD5_Init(&c);
        MD5_Update(&c, str, strlen(str));
        MD5_Final(hash, &c);

        sprintf(hexhash, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", hash[0], hash[1], hash[2], hash[3], hash[4], hash[5],hash[6],hash[7], hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);

	return hexhash;
	}

/* Decompressing a gzipped or deflated page is different enough from the stuff
 * in dcmp_page that we need a separate function for it. On the bright side, we
 * could more easily add support for bzip2 if ever needed. */

char *decomp(server *srv, plugin_data *p, char **page, size_t *val){
#ifdef USE_ZLIB
        z_stream *zstr;
        int zerr, zr;
        unsigned int obsize, tsize, have;
        unsigned int ib = 0;
        zerr = zr = obsize = tsize = 0;
        unsigned char *in;
        unsigned char *out;
        unsigned char *outbuf = malloc(0);
        unsigned char *ob2;

	/* If we're in this function, just assume that this is compressed data.
	 * NOTE: I'm not entirely sure if setting windowBits + 32 will handle
	 * raw inflate or not. I guess if it blows up sometime we'll know. */

	if (p->conf.debug || p->conf.zlib_debug)
		MCLOGERR("Decompressing compressed page.");

	zstr = (z_stream *)malloc(sizeof(z_stream));
        zstr->zalloc = Z_NULL;
        zstr->zfree = Z_NULL;
        zstr->opaque = Z_NULL;
        zstr->next_in = Z_NULL;
        zstr->avail_in = 0;
        /* init buffers */
        in = malloc(Z_BUFSIZE);
        out = malloc(Z_BUFSIZE);
        zr = inflateInit2(zstr, GZPLUSZ);
        if (zr != Z_OK){
                if (p->conf.debug || p->conf.zlib_debug)
                        log_error_write(srv, __FILE__, __LINE__, "ss", "inflateInit2 in decomp failed with: ", zstr->msg ? zstr->msg : "no message");

                /* Just bail out silently. We use the harmful goto
                 * to make sure everything's cleaned up. 
                 * Don't screw with the page we were decompressing - just send
		 * it back and hope for the best.
                 */
                goto harmful;
                }
	do { /* begin first do/while */
		/* Read in string from memcached */
                zstr->avail_in = Z_BUFSIZE;
                memcpy(in, (*page)+ib,Z_BUFSIZE);
                ib += Z_BUFSIZE;
                zstr->next_in = in;
                do { /* begin second do/while */
                        zstr->avail_out = Z_BUFSIZE;
                        zstr->next_out = out;
                        zerr = inflate(zstr, Z_SYNC_FLUSH);
                        /* checking for errors with code not only
                         * stolen from the zlib howto, but code I wrote
                         * using code stolen from the zlib code from
                         * before. w00!
                         */
                        switch (zerr) {
                                case Z_NEED_DICT:
                                        zerr = Z_DATA_ERROR;
                                case Z_DATA_ERROR:
                                case Z_MEM_ERROR:
                                case Z_STREAM_ERROR:
                                        if (p->conf.debug || p->conf.zlib_debug)
                                                log_error_write(srv, __FILE__, __LINE__, "ss", "inflate failed with: ", zstr->msg ? zstr->msg : "no message");
                                        goto harmful;
                                }
			have = Z_BUFSIZE - zstr->avail_out;
                        tsize = obsize;
                        obsize += have;
                        ob2 = realloc(outbuf, obsize);
                        if (ob2 == NULL){
                                log_error_write(srv, __FILE__, __LINE__, "s", "reallocing outbuf failed!");
                                if(tsize != 0)
                                        goto harmful;
                                }
                        else {
                                outbuf = ob2;
                                }
                        memmove(outbuf+tsize,out,have);
                        } while (zstr->avail_out == 0); /* end second
                                                        * do/while */
                } while (zerr != Z_STREAM_END); /* end first do/while */
	harmful:
	if(zerr == Z_STREAM_END){
                outbuf[zstr->total_out] = '\0';

                if (p->conf.debug || p->conf.zlib_debug)
			MCLOGERR("Uncompressed page successfully");

                /* this should set value_length correctly */
                *val = zstr->total_out;
                }
        else {
                if (p->conf.debug || p->conf.zlib_debug)
                        log_error_write(srv, __FILE__, __LINE__, "sdsd", "Uncompressing content failed: read in ", zstr->total_in, "wrote out ", zstr->total_out);
                }
        (void)inflateEnd(zstr);
        /* *should* we be freeing zstr? */
        free(zstr);
        free(in);
        free(out);
	/* and send back outbuf */
	return (char *)outbuf;
#else
	/* Send NULL, I guess. */
	return NULL;
#endif
	}

#else
int mod_mcpage_plugin_init(plugin *p) {
        UNUSED(p);
        return -1;
}
#endif
