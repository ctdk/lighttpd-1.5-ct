#ifndef _MOD_MCPAGE_H
#define _MOD_MCPAGE_H
#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"
#include "response.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined HAVE_ZLIB_H && defined HAVE_LIBZ
#define USE_ZLIB
#include <zlib.h>

/* zlib related defines */
#define Z_BUFSIZE       16384
#define GZWBITS		MAX_WBITS + 16
#define GZPLUSZ		MAX_WBITS + 32
#define	GZ_COMP_LEV	8

#endif

#ifdef USE_OPENSSL
#include <openssl/md5.h>
#else
#include "md5.h"
#endif

#define MCLOGERR(a)	if (p->conf.debug || p->conf.zlib_debug)\
				log_error_write(srv, __FILE__, __LINE__, "s", a)

#define MCLOG(a) log_error_write(srv, __FILE__, __LINE__, "s", a)

/* How many microseconds do we wait before timing out and bailing? Only use
 * this if we've set non-block. Trying it out as a way to get non-blocking type
 * behavior working with memcached_get -- the async stuff available with mget
 * and fetch has bizarre consequences elsewhere in the program. */

#define MC_SEND_TIMEOUT	250000

/* libmemcached includes (since libmemcached, not libmemache, is the new 
 * hotness. Plus, it's maintained).
 */

#ifdef HAVE_MEMCACHED_H
#include <libmemcached/memcached.h>

/* memcached structure and plugin defines */

#define MCPAGE_CONFIG_MEMCACHED_ENABLE		"mcpage.enable"
#define MCPAGE_CONFIG_MEMCACHED_HOSTS		"mcpage.memcached-hosts"
#define MCPAGE_CONFIG_MEMCACHED_NS		"mcpage.memcached-namespace"
#define MCPAGE_CONFIG_MEMCACHED_KEYPREFIX	"mcpage.memcached-keyprefix"
#define MCPAGE_CONFIG_MEMCACHED_HASHING		"mcpage.memcached-hashing"
#define MCPAGE_CONFIG_MEMCACHED_BEHAVIOR	"mcpage.memcached-behavior"
#define MCPAGE_CONFIG_MEMCACHED_EXPIRE		"mcpage.memcached-expire"
#define MCPAGE_CONFIG_MEMCACHED_MINSIZE		"mcpage.memcached-minsize"
#define MCPAGE_CONFIG_DEBUG			"mcpage.debug"
#define MCPAGE_CONFIG_MEMCACHED_DEBUG		"mcpage.memcached-debug"
#define MCPAGE_CONFIG_ZLIB_DEBUG		"mcpage.zlib-debug"
#define MCPAGE_CONFIG_MEMCACHED_NOBLOCK		"mcpage.memcached-noblock"
#define MCPAGE_CONFIG_MEMCACHED_PUT		"mcpage.memcached-put"
#define MCPAGE_CONFIG_DEBUG_VERBOSE		"mcpage.debug-verbose"
#define MCPAGE_CONFIG_LOCALMC_ENABLE            "mcpage.localmc-enable"
#define MCPAGE_CONFIG_LOCALMC_EXPIRE		"mcpage.localmc-expire"
#define MCPAGE_CONFIG_LOCALMC_ADDR		"mcpage.localmc-addr"
#define MCPAGE_CONFIG_LOCALMC_SOCKET		"mcpage.localmc-socket"
#define MCPAGE_CONFIG_MEMCACHED_BINARY		"mcpage.memcached-binary"
#define MCPAGE_CONFIG_MD5			"mcpage.md5"
#define MCPAGE_CONFIG_ANNOUNCE			"mcpage.announce"

/* Max memcached object size in KB. If you've modified memcached to accept
 * larger objects, redefine this. */
#define MC_MAX_SIZE	1024

/* Defines for sniffing out binary data */
#define MC_INS_LIMIT 100
#define MC_BYTE_PER 25

/* Local memcached expiry default */
#define LMC_DEFAULT_EXPIRY	5

/* plugin config for all request/connections */

typedef struct {
        unsigned short enable; 
        array *mc_hosts;
        buffer *mc_namespace;
        buffer *mc_keyprefix;
	buffer *mc_hashing;
	buffer *mc_behavior;
	buffer *lmc_addr;
	buffer *lmc_socket;
        struct memcached_st *mc;
	struct memcached_st *lmc;
	unsigned short mc_expire;
	unsigned short mc_minsize;
	unsigned short debug;
	unsigned short mc_debug;
	unsigned short zlib_debug;
	unsigned short mc_noblock;
	unsigned short mc_put;
	unsigned short debug_verbose;
	unsigned short lmc_enable;
	unsigned short lmc_expire;
	unsigned short mc_binary;
	unsigned short md5;
	unsigned short announce;
} plugin_config;

typedef struct {
        PLUGIN_DATA;

        buffer *match_buf;

        plugin_config **config_storage;

        plugin_config conf;
} plugin_data;

typedef struct {
	buffer *outpg;
	buffer *content_type;
	/* Expires stuff */
	buffer *expires;
	buffer *cache_control;
	unsigned short done;
	short nocomp;
	unsigned short deflate;
} handler_ctx;

/* structures for what we're putting into memached. We'll have to store the
 * string (or memory area, if it's gzipped) *and* the length in it, because we
 * *will* have null bytes to deal with.
 */

typedef struct {
	buffer *content_type;
	buffer *outpg;
	} mcpre;

typedef struct {
	size_t mclen;
	char *uri;
	char *page;
	} mcstore;

/* function declarations */
char * check_mc_page(server *srv, connection *con, plugin_data *p ,char *r, size_t *value_length);
void dcmp_page(server *srv, char **chkpage, char *r, plugin_data *p, size_t *value_length);
mcstore * prep_for_memcached(server *srv, connection *con, plugin_data *p, handler_ctx *pagestore);
void compress_page(server *srv, connection *con, plugin_data *p, mcstore **mccontent);
mcstore * mcstore_init();
void mcstore_free(mcstore *mccontent);
mcpre * mcpre_init();
void mcpre_free(mcpre *pagestore);
void store_mc_page(server *srv, connection *con, plugin_data *p, mcstore *mccontent);
char *md5hash(char *str);
char *decomp(server *srv, plugin_data *p, char **page, size_t *val);

#endif

#endif
