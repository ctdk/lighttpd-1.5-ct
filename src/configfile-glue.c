#include <string.h>

#include "base.h"
#include "buffer.h"
#include "array.h"
#include "log.h"

/**
 * like all glue code this file contains functions which
 * are the external interface of lighttpd. The functions
 * are used by the server itself and the plugins.
 *
 * The main-goal is to have a small library in the end 
 * which is linked against both and which will define 
 * the interface itself in the end.
 * 
 */


/* handle global options */

/* parse config array */
int config_insert_values_internal(server *srv, array *ca, const config_values_t cv[]) {
	size_t i;
	data_unset *du;
	
	for (i = 0; cv[i].key; i++) {
		
		if (NULL == (du = array_get_element(ca, cv[i].key))) {
			/* no found */
			
			continue;
		}
		
		switch (cv[i].type) {
		case T_CONFIG_ARRAY:
			if (du->type == TYPE_ARRAY) {
				size_t j;
				data_array *da = (data_array *)du;
				
				for (j = 0; j < da->value->used; j++) {
					if (da->value->data[j]->type == TYPE_STRING) {
						data_string *ds = data_string_init();
						
						buffer_copy_string_buffer(ds->value, ((data_string *)(da->value->data[j]))->value);
						buffer_copy_string_buffer(ds->key, ((data_string *)(da->value->data[j]))->key);
						
						array_insert_unique(cv[i].destination, (data_unset *)ds);
					} else {
						log_error_write(srv, __FILE__, __LINE__, "sssbs", "unexpected type for key: ", cv[i].key, "[", da->value->data[i]->key, "](string)");
						
						return -1;
					}
				}
			} else {
				log_error_write(srv, __FILE__, __LINE__, "sss", "unexpected type for key: ", cv[i].key, "array of strings");
				
				return -1;
			}
			break;
		case T_CONFIG_STRING:
			if (du->type == TYPE_STRING) {
				data_string *ds = (data_string *)du;
				
				buffer_copy_string_buffer(cv[i].destination, ds->value);
			} else {
				log_error_write(srv, __FILE__, __LINE__, "ssss", "unexpected type for key: ", cv[i].key, "(string)", "\"...\"");
				
				return -1;
			}
			break;
		case T_CONFIG_SHORT:
			switch(du->type) {
			case TYPE_INTEGER: {
				data_integer *di = (data_integer *)du;
				
				*((unsigned short *)(cv[i].destination)) = di->value;
				break;
			}
			case TYPE_STRING: {
				data_string *ds = (data_string *)du;
					
				log_error_write(srv, __FILE__, __LINE__, "ssbss", "unexpected type for key: ", cv[i].key, ds->value, "(short)", "0 ... 65535");
				
				return -1;
			}
			default:
				log_error_write(srv, __FILE__, __LINE__, "ssdss", "unexpected type for key: ", cv[i].key, du->type, "(short)", "0 ... 65535");
				return -1;
			}
			break;
		case T_CONFIG_BOOLEAN:
			if (du->type == TYPE_STRING) {
				data_string *ds = (data_string *)du;
				
				if (buffer_is_equal_string(ds->value, CONST_STR_LEN("enable"))) {
					*((unsigned short *)(cv[i].destination)) = 1;
				} else if (buffer_is_equal_string(ds->value, CONST_STR_LEN("disable"))) {
					*((unsigned short *)(cv[i].destination)) = 0;
				} else {
					log_error_write(srv, __FILE__, __LINE__, "ssbs", "ERROR: unexpected value for key:", cv[i].key, ds->value, "(enable|disable)");
						
					return -1;
				}
			} else {
				log_error_write(srv, __FILE__, __LINE__, "ssss", "ERROR: unexpected type for key:", cv[i].key, "(string)", "\"(enable|disable)\"");
				
				return -1;
			}
			break;
		case T_CONFIG_LOCAL:
		case T_CONFIG_UNSET:
			break;
		case T_CONFIG_DEPRECATED:
			log_error_write(srv, __FILE__, __LINE__, "ssss", "ERROR: found deprecated key:", cv[i].key, "-", (char *)(cv[i].destination));
			
			srv->config_deprecated = 1;
			
			break;
		}
	}
	return 0;
}

int config_insert_values_global(server *srv, array *ca, const config_values_t cv[]) {
	size_t i;
	data_unset *du;
	
	for (i = 0; cv[i].key; i++) {
		data_string *touched;
		
		if (NULL == (du = array_get_element(ca, cv[i].key))) {
			/* no found */
			
			continue;
		}
		
		/* touched */
		touched = data_string_init();
		
		buffer_copy_string(touched->value, "");
		buffer_copy_string_buffer(touched->key, du->key);
		
		array_insert_unique(srv->config_touched, (data_unset *)touched);
	}
	
	return config_insert_values_internal(srv, ca, cv);
}

unsigned short sock_addr_get_port(sock_addr *addr) {
#ifdef HAVE_IPV6
	return ntohs(addr->plain.sa_family ? addr->ipv6.sin6_port : addr->ipv4.sin_port);
#else
	return ntohs(addr->ipv4.sin_port);
#endif
}

int config_check_cond(server *srv, connection *con, data_config *dc) {
	server_socket *srv_sock = con->srv_socket;
	buffer *l;
	
	/* pass the rules */
	
	/* 
	 * OPTIMIZE
	 * 
	 * - replace all is_equal be simple == to an enum
	 * - only check each condition once at the start of the request
	 *   afterwards they will always evaluate in the same way 
	 *   as they have the same input data
	 * 
	 */
	
	buffer_copy_string(srv->cond_check_buf, "");
	
	if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("HTTPhost"))) {
		char *ck_colon = NULL, *val_colon = NULL;
		
		if (!buffer_is_empty(con->uri.authority)) {
		
			/* 
			 * append server-port to the HTTP_POST if necessary
			 */
			
			buffer_copy_string_buffer(srv->cond_check_buf, con->uri.authority);
			
			switch(dc->cond) {
			case CONFIG_COND_NE:
			case CONFIG_COND_EQ:
				ck_colon = strchr(dc->string->ptr, ':');
				val_colon = strchr(con->uri.authority->ptr, ':');
				
				if (ck_colon && !val_colon) {
					/* colon found */
					BUFFER_APPEND_STRING_CONST(srv->cond_check_buf, ":");
					buffer_append_long(srv->cond_check_buf, sock_addr_get_port(&(srv_sock->addr)));
				}
				break;
			default:
				break;
			}
		}
	} else if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("HTTPremoteip"))) {
		char *nm_slash;
		/* handle remoteip limitations 
		 * 
		 * "10.0.0.1" is provided for all comparisions
		 * 
		 * only for == and != we support
		 * 
		 * "10.0.0.1/24"
		 */
		
		if ((dc->cond == CONFIG_COND_EQ ||
		     dc->cond == CONFIG_COND_EQ) &&
		    (con->dst_addr.plain.sa_family == AF_INET) &&
		    (NULL != (nm_slash = strchr(dc->string->ptr, '/')))) {
			int nm_bits;
			long nm;
			char *err;
			struct in_addr val_inp;
			
			if (*(nm_slash+1) == '\0') {
				log_error_write(srv, __FILE__, __LINE__, "sb", "ERROR: no number after / ", dc->string);
					
				return 0;
			}
			
			nm_bits = strtol(nm_slash + 1, &err, 10);
			
			if (*err) {
				log_error_write(srv, __FILE__, __LINE__, "sbs", "ERROR: non-digit found in netmask:", dc->string, *err);
				
				return 0;
			}
			
			/* take IP convert to the native */
			buffer_copy_string_len(srv->cond_check_buf, dc->string->ptr, nm_slash - dc->string->ptr);
#ifdef __WIN32			
			if (INADDR_NONE == (val_inp.s_addr = inet_addr(srv->cond_check_buf->ptr))) {
				log_error_write(srv, __FILE__, __LINE__, "sb", "ERROR: ip addr is invalid:", srv->cond_check_buf);
				
				return 0;
			}

#else
			if (0 == inet_aton(srv->cond_check_buf->ptr, &val_inp)) {
				log_error_write(srv, __FILE__, __LINE__, "sb", "ERROR: ip addr is invalid:", srv->cond_check_buf);
				
				return 0;
			}
#endif
			
			/* build netmask */
			nm = htonl(~((1 << (32 - nm_bits)) - 1));
			
			if ((val_inp.s_addr & nm) == (con->dst_addr.ipv4.sin_addr.s_addr & nm)) {
				return (dc->cond == CONFIG_COND_EQ) ? 1 : 0;
			} else {
				return (dc->cond == CONFIG_COND_EQ) ? 0 : 1;
			}
		} else {
			const char *s;
#ifdef HAVE_IPV6
			char b2[INET6_ADDRSTRLEN + 1];
			
			s = inet_ntop(con->dst_addr.plain.sa_family, 
				      con->dst_addr.plain.sa_family == AF_INET6 ? 
				      (const void *) &(con->dst_addr.ipv6.sin6_addr) :
				      (const void *) &(con->dst_addr.ipv4.sin_addr),
				      b2, sizeof(b2)-1);
#else
			s = inet_ntoa(con->dst_addr.ipv4.sin_addr);
#endif
			buffer_copy_string(srv->cond_check_buf, s);
		}
	} else if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("HTTPurl"))) {
		buffer_copy_string_buffer(srv->cond_check_buf, con->uri.path);
	} else if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("SERVERsocket"))) {
		buffer_copy_string_buffer(srv->cond_check_buf, srv_sock->srv_token);
	} else if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("HTTPreferer"))) {
		data_string *ds;
		
		if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "Referer"))) {
			buffer_copy_string_buffer(srv->cond_check_buf, ds->value);
		}
	} else if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("HTTPcookie"))) {
		data_string *ds;
		if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "Cookie"))) {
			buffer_copy_string_buffer(srv->cond_check_buf, ds->value);
		}
	} else if (buffer_is_equal_string(dc->comp_key, CONST_STR_LEN("HTTPuseragent"))) {
		data_string *ds;
		if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "User-Agent"))) {
			buffer_copy_string_buffer(srv->cond_check_buf, ds->value);
		}
	} else {
		return 0;
	}
	
	l = srv->cond_check_buf;
	
	switch(dc->cond) {
	case CONFIG_COND_NE:
	case CONFIG_COND_EQ:
		if (buffer_is_equal(l, dc->string)) {
			return (dc->cond == CONFIG_COND_EQ) ? 1 : 0;
		} else {
			return (dc->cond == CONFIG_COND_EQ) ? 0 : 1;
		}
		break;

	case CONFIG_COND_NOMATCH:
	case CONFIG_COND_MATCH: {
#ifdef HAVE_PCRE_H
#define N 10
		int ovec[N * 3];
		int n;
		
		n = pcre_exec(dc->regex, dc->regex_study, l->ptr, l->used - 1, 0, 0, ovec, N * 3);
		
		if (n > 0) {
			return (dc->cond == CONFIG_COND_MATCH) ? 1 : 0;
		} else {
			return (dc->cond == CONFIG_COND_MATCH) ? 0 : 1;
		}
#endif		
		break;
	}

	default:
		/* no way */
		break;
	}
	
	return 0;
}

