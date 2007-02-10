INCLUDE(CheckIncludeFiles)
INCLUDE(CheckFunctionExists)
INCLUDE(CheckVariableExists)
INCLUDE(CheckTypeSize)
INCLUDE(CheckLibraryExists)
INCLUDE(CMakeDetermineCCompiler)
INCLUDE(FindThreads)
INCLUDE(CPack)
INCLUDE(UsePkgConfig)

INCLUDE(LighttpdMacros)

ADD_DEFINITIONS(-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGE_FILES)

OPTION(WITH_XATTR "with xattr-support for the stat-cache [default: off]")
OPTION(WITH_MYSQL "with mysql-support for the mod_sql_vhost [default: off]")
OPTION(WITH_OPENSSL "with openssl-support [default: off]")
OPTION(WITH_PCRE "with regex support [default: on]" ON)
OPTION(WITH_WEBPAV_PROPS "with property-support for mod_webdav [default: off]")
OPTION(WITH_BZIP "with bzip2-support for mod_compress [default: off]")
OPTION(WITH_ZLIB "with deflate-support for mod_compress [default: on]" ON)
OPTION(WITH_LDAP "with LDAP-support for the mod_auth [default: off]")
OPTION(WITH_LIBAIO "with libaio for the linux [default: off]")
OPTION(WITH_LIBFCGI "with libfcgi for fcgi-stat-accel [default: off]")
OPTION(WITH_LUA "with lua 5.1 for mod_magnet [default: off]")

OPTION(BUILD_STATIC "build a static lighttpd with all modules added")
IF(BUILD_STATIC)
  SET(LIGHTTPD_STATIC 1)
ELSE(BUILD_STATIC)
  SET(CMAKE_SHARED_LIBRARY_PREFIX "")
ENDIF(BUILD_STATIC)

IF(WITH_WEBPAV_PROPS)
  SET(WITH_XML 1)
  SET(WITH_SQLITE3 1)
  SET(WITH_UUID 1)
ENDIF(WITH_WEBPAV_PROPS)

CHECK_INCLUDE_FILES(sys/devpoll.h HAVE_SYS_DEVPOLL_H)
CHECK_INCLUDE_FILES(sys/epoll.h HAVE_SYS_EPOLL_H)
CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)
CHECK_INCLUDE_FILES(sys/mman.h HAVE_SYS_MMAN_H)
CHECK_INCLUDE_FILES(sys/poll.h HAVE_SYS_POLL_H)
CHECK_INCLUDE_FILES(sys/port.h HAVE_SYS_PORT_H)
CHECK_INCLUDE_FILES(sys/prctl.h HAVE_SYS_PRCTL_H)
CHECK_INCLUDE_FILES(sys/resource.h HAVE_SYS_RESOURCE_H)
CHECK_INCLUDE_FILES(sys/sendfile.h HAVE_SYS_SENDFILE_H)
CHECK_INCLUDE_FILES(sys/select.h HAVE_SYS_SELECT_H)
CHECK_INCLUDE_FILES(sys/syslimits.h HAVE_SYS_SYSLIMITS_H)
CHECK_INCLUDE_FILES(sys/types.h HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILES(sys/uio.h HAVE_SYS_UIO_H)
CHECK_INCLUDE_FILES(sys/un.h HAVE_SYS_UN_H)
CHECK_INCLUDE_FILES(sys/wait.h HAVE_SYS_WAIT_H)
CHECK_INCLUDE_FILES(sys/time.h HAVE_SYS_TIME_H)
CHECK_INCLUDE_FILES(time.h HAVE_TIME_H)

PKGCONFIG(gthread-2.0 GTHREAD_INCDIR GTHREAD_LIBDIR GTHREAD_LDFLAGS GTHREAD_CFLAGS)

MESSAGE(STATUS "found glib-2.0 at: ${GTHREAD_INCDIR} ${GTHREAD_LIBDIR} ${GTHREAD_LDFLAGS} ${GTHREAD_CFLAGS}")

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GTHREAD_CFLAGS}")

IF(WITH_XATTR)
  CHECK_INCLUDE_FILES(attr/attributes.h HAVE_ATTR_ATTRIBUTES_H)
ENDIF(WITH_XATTR)

IF(WITH_MYSQL)
  SET(CMAKE_REQUIRED_INCLUDES /usr/include/mysql)
  CHECK_INCLUDE_FILES(mysql.h HAVE_MYSQL_H)
  SET(CMAKE_REQUIRED_INCLUDES)
  IF(HAVE_MYSQL_H)
    CHECK_LIBRARY_EXISTS(mysqlclient mysql_real_connect "" HAVE_LIBMYSQL)
  ENDIF(HAVE_MYSQL_H)
ENDIF(WITH_MYSQL)

IF(WITH_OPENSSL)
  CHECK_INCLUDE_FILES(openssl/ssl.h HAVE_OPENSSL_SSL_H)
  IF(HAVE_OPENSSL_SSL_H)
    CHECK_LIBRARY_EXISTS(crypto BIO_f_base64 "" HAVE_LIBCRYPTO)
    IF(HAVE_LIBCRYPTO)
      SET(OPENSSL_NO_KRB5 1)
      CHECK_LIBRARY_EXISTS(ssl SSL_new "" HAVE_LIBSSL)
    ENDIF(HAVE_LIBCRYPTO)
  ENDIF(HAVE_OPENSSL_SSL_H)
ENDIF(WITH_OPENSSL)

CHECK_INCLUDE_FILES(aio.h HAVE_AIO_H)
IF(WITH_BZIP)
  CHECK_INCLUDE_FILES(bzlib.h HAVE_BZLIB_H)
  CHECK_LIBRARY_EXISTS(bz2 BZ2_bzCompressInit "" HAVE_LIBBZ2)
ENDIF(WITH_BZIP)

CHECK_INCLUDE_FILES(getopt.h HAVE_GETOPT_H)
CHECK_INCLUDE_FILES(inttypes.h HAVE_INTTYPES_H)
IF(WITH_LDAP)
  CHECK_INCLUDE_FILES(ldap.h HAVE_LDAP_H)
  CHECK_LIBRARY_EXISTS(ldap ldap_open "" HAVE_LIBLDAP)
ENDIF(WITH_LDAP)

IF(WITH_LIBAIO)
  CHECK_INCLUDE_FILES(libaio.h HAVE_LIBAIO_H)
  CHECK_LIBRARY_EXISTS(aio io_getevents "" HAVE_LIBAIO)
ENDIF(WITH_LIBAIO)

IF(WITH_XML)
  CHECK_INCLUDE_FILES(libxml.h HAVE_LIBXML_H)
  CHECK_LIBRARY_EXISTS(xml2 xmlParseChunk "" HAVE_LIBXML)
ENDIF(WITH_XML)

IF(WITH_PCRE)
  CHECK_INCLUDE_FILES(pcre.h HAVE_PCRE_H)
  CHECK_LIBRARY_EXISTS(pcre pcre_exec "" HAVE_LIBPCRE)
ENDIF(WITH_PCRE)

CHECK_INCLUDE_FILES(poll.h HAVE_POLL_H)
CHECK_INCLUDE_FILES(pwd.h HAVE_PWD_H)

OPTION(WITH_SQLITE3 "with property-support [sqlite3] for mod_webdav [default: off]")
IF(WITH_SQLITE3)
  CHECK_INCLUDE_FILES(sqlite3.h HAVE_SQLITE3_H)
  CHECK_LIBRARY_EXISTS(sqlite3 sqlite3_reset "" HAVE_LIBPCRE)
ENDIF(WITH_SQLITE3)

IF(WITH_LIBFCGI)
  CHECK_INCLUDE_FILES(fastcgi.h HAVE_FASTCGI_H)
  CHECK_INCLUDE_FILES(fastcgi/fastcgi.h HAVE_FASTCGI_FASTCGI_H)
  IF(HAVE_FASTCGI_H)
    CHECK_LIBRARY_EXISTS(fcgi FCGI_Accept "" HAVE_LIBFCGI)
  ENDIF(HAVE_FASTCGI_H)
ENDIF(WITH_LIBFCGI)

CHECK_INCLUDE_FILES(stddef.h HAVE_STDDEF_H)
CHECK_INCLUDE_FILES(stdint.h HAVE_STDINT_H)
CHECK_INCLUDE_FILES(syslog.h HAVE_SYSLOG_H)
IF(WITH_UUID)
  CHECK_INCLUDE_FILES(uuid/uuid.h HAVE_UUID_H)
  CHECK_LIBRARY_EXISTS(uuid uuid_generate "" HAVE_LIBUUID)
ENDIF(WITH_UUID)

CHECK_INCLUDE_FILES(sys/inotify.h HAVE_SYS_INOTIFY_H)
IF(HAVE_SYS_INOTIFY_H)
  CHECK_FUNCTION_EXISTS(inotify_init HAVE_INOTIFY_INIT)
ENDIF(HAVE_SYS_INOTIFY_H)

IF(WITH_ZLIB)
  CHECK_INCLUDE_FILES(zlib.h HAVE_ZLIB_H)
  CHECK_LIBRARY_EXISTS(z deflate "" HAVE_LIBZ)
ENDIF(WITH_ZLIB)

IF(WITH_LUA)
  SET(CMAKE_REQUIRED_LIBRARIES "-lm")
  CHECK_INCLUDE_FILES(lua.h HAVE_LUA_H)
  CHECK_LIBRARY_EXISTS(lua lua_setfield "" HAVE_LIBLUA)
  SET(CMAKE_REQUIRED_LIBRARIES)
ENDIF(WITH_LUA)

IF(NOT BUILD_STATIC)
  CHECK_INCLUDE_FILES(dlfcn.h HAVE_DLFCN_H)
  CHECK_LIBRARY_EXISTS(dl dlopen "" HAVE_LIBDL)
ENDIF(NOT BUILD_STATIC)

SET(CMAKE_EXTRA_INCLUDE_FILES sys/socket.h)
CHECK_TYPE_SIZE(socklen_t HAVE_SOCKLEN_T)
SET(CMAKE_EXTRA_INCLUDE_FILES)

CHECK_TYPE_SIZE(long SIZEOF_LONG)
CHECK_TYPE_SIZE(off_t SIZEOF_OFF_T)

CHECK_FUNCTION_EXISTS(chroot HAVE_CHROOT)
CHECK_FUNCTION_EXISTS(crypt HAVE_CRYPT)
CHECK_FUNCTION_EXISTS(epoll_ctl HAVE_EPOLL_CTL)
CHECK_FUNCTION_EXISTS(fork HAVE_FORK)
CHECK_FUNCTION_EXISTS(getrlimit HAVE_GETRLIMIT)
CHECK_FUNCTION_EXISTS(getuid HAVE_GETUID)
CHECK_FUNCTION_EXISTS(gmtime_r HAVE_GMTIME_R)
CHECK_FUNCTION_EXISTS(inet_ntop HAVE_INET_NTOP)
CHECK_FUNCTION_EXISTS(kqueue HAVE_KQUEUE)
CHECK_FUNCTION_EXISTS(localtime_r HAVE_LOCALTIME_R)
CHECK_FUNCTION_EXISTS(lstat HAVE_LSTAT)
CHECK_FUNCTION_EXISTS(madvise HAVE_MADVISE)
CHECK_FUNCTION_EXISTS(memcpy HAVE_MEMCPY)
CHECK_FUNCTION_EXISTS(memset HAVE_MEMSET)
CHECK_FUNCTION_EXISTS(mmap HAVE_MMAP)
CHECK_FUNCTION_EXISTS(pathconf HAVE_PATHCONF)
CHECK_FUNCTION_EXISTS(poll HAVE_POLL)
CHECK_FUNCTION_EXISTS(port_create HAVE_PORT_CREATE)
CHECK_FUNCTION_EXISTS(prctl HAVE_PRCTL)
CHECK_FUNCTION_EXISTS(posix_fadvise HAVE_POSIX_FADVISE)
CHECK_FUNCTION_EXISTS(select HAVE_SELECT)
CHECK_FUNCTION_EXISTS(sendfile HAVE_SENDFILE)
CHECK_FUNCTION_EXISTS(sendfile64 HAVE_SENDFILE64)
CHECK_FUNCTION_EXISTS(sendfilev HAVE_SENDFILEV)
CHECK_FUNCTION_EXISTS(sigaction HAVE_SIGACTION)
CHECK_FUNCTION_EXISTS(signal HAVE_SIGNAL)
CHECK_FUNCTION_EXISTS(sigtimedwait HAVE_SIGTIMEDWAIT)
CHECK_FUNCTION_EXISTS(strptime HAVE_STRPTIME)
CHECK_FUNCTION_EXISTS(syslog HAVE_SYSLOG)
CHECK_FUNCTION_EXISTS(writev HAVE_WRITEV)
CHECK_FUNCTION_EXISTS(inet_aton HAVE_INET_ATON)
IF(HAVE_CRYPT_H)
  ## check if we need libcrypt for crypt()
  CHECK_LIBRARY_EXISTS(crypt crypt "" HAVE_LIBCRYPT)
ENDIF(HAVE_CRYPT_H)

ADD_DEFINITIONS(
  -DLIGHTTPD_VERSION_ID=10500
  -DPACKAGE_NAME="\\"${CMAKE_PROJECT_NAME}\\""
  -DLIBRARY_DIR="\\"${CMAKE_INSTALL_PREFIX}/lib\\""
  -DPACKAGE_VERSION="\\"${CPACK_PACKAGE_VERSION}\\""
  )

## Write out config.h
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

ADD_DEFINITIONS(-DHAVE_CONFIG_H)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

SET(COMMON_SRC
      buffer.c log.c
      keyvalue.c chunk.c
      stream.c fdevent.c
      stat_cache.c plugin.c joblist.c etag.c array.c
      data_string.c data_count.c data_array.c
      data_integer.c md5.c
      fdevent_select.c fdevent_linux_rtsig.c
      fdevent_poll.c fdevent_linux_sysepoll.c
      fdevent_solaris_devpoll.c fdevent_freebsd_kqueue.c
      data_config.c bitset.c
      inet_ntop_cache.c crc32.c
      connections-glue.c iosocket.c
      configfile-glue.c
      http-header-glue.c status_counter.c
      splaytree.c
      network_writev.c
      network_write.c
      network_linux_sendfile.c
      network_freebsd_sendfile.c
      network_win32_send.c
      network_solaris_sendfilev.c
      network_openssl.c
      network_linux_aio.c
      network_posix_aio.c
      network_gthread_aio.c
      http_resp.c
      http_resp_parser.c
      http_req.c
      http_req_parser.c
      http_req_range.c
      http_req_range_parser.c
      sys-files.c
      sys-socket.c
      filter.c
)

ADD_EXECUTABLE(lemon lemon.c)

## Build parsers by using lemon...
LEMON_PARSER(configparser.y)
LEMON_PARSER(http_req_parser.y)
LEMON_PARSER(http_req_range_parser.y)
LEMON_PARSER(http_resp_parser.y)
LEMON_PARSER(mod_ssi_exprparser.y)

SET(L_INSTALL_TARGETS)

IF(HAVE_LIBFCGI)
  ADD_EXECUTABLE(fcgi-stat-accel fcgi-stat-accel.c)
  TARGET_LINK_LIBRARIES(fcgi-stat-accel fcgi)
  SET_TARGET_PROPERTIES(fcgi-stat-accel PROPERTIES LINK_FLAGS "-pthread")
ENDIF(HAVE_LIBFCGI)

ADD_EXECUTABLE(spawn-fcgi spawn-fcgi.c)

ADD_EXECUTABLE(lighttpd
	server.c
	network.c
	configfile.c
	configparser.c
	connections.c
	proc_open.c
	request.c
	response.c
	${COMMON_SRC})

ADD_AND_INSTALL_LIBRARY(mod_access mod_access.c)
ADD_AND_INSTALL_LIBRARY(mod_alias mod_alias.c)
ADD_AND_INSTALL_LIBRARY(mod_dirlisting mod_dirlisting.c)
ADD_AND_INSTALL_LIBRARY(mod_staticfile mod_staticfile.c)

ADD_AND_INSTALL_LIBRARY(mod_indexfile mod_indexfile.c)
ADD_AND_INSTALL_LIBRARY(mod_setenv mod_setenv.c)
ADD_AND_INSTALL_LIBRARY(mod_rrdtool mod_rrdtool.c)
ADD_AND_INSTALL_LIBRARY(mod_usertrack mod_usertrack.c)
ADD_AND_INSTALL_LIBRARY(mod_proxy_core 	"mod_proxy_core.c;mod_proxy_core_pool.c;mod_proxy_core_backend.c;mod_proxy_core_address.c;mod_proxy_core_backlog.c;mod_proxy_core_protocol.c;mod_proxy_core_rewrites.c")
ADD_AND_INSTALL_LIBRARY(mod_proxy_backend_http mod_proxy_backend_http.c)
ADD_AND_INSTALL_LIBRARY(mod_proxy_backend_fastcgi mod_proxy_backend_fastcgi.c)
ADD_AND_INSTALL_LIBRARY(mod_proxy_backend_scgi mod_proxy_backend_scgi.c)
ADD_AND_INSTALL_LIBRARY(mod_proxy_backend_ajp13 mod_proxy_backend_ajp13.c)
ADD_AND_INSTALL_LIBRARY(mod_userdir mod_userdir.c)
ADD_AND_INSTALL_LIBRARY(mod_cgi mod_cgi.c)
ADD_AND_INSTALL_LIBRARY(mod_secdownload mod_secure_download.c)
ADD_AND_INSTALL_LIBRARY(mod_accesslog mod_accesslog.c)
ADD_AND_INSTALL_LIBRARY(mod_simple_vhost mod_simple_vhost.c)
ADD_AND_INSTALL_LIBRARY(mod_evhost mod_evhost.c)
ADD_AND_INSTALL_LIBRARY(mod_expire mod_expire.c)
ADD_AND_INSTALL_LIBRARY(mod_status mod_status.c)
ADD_AND_INSTALL_LIBRARY(mod_compress mod_compress.c)
ADD_AND_INSTALL_LIBRARY(mod_deflate mod_deflate.c)
ADD_AND_INSTALL_LIBRARY(mod_redirect mod_redirect.c)
ADD_AND_INSTALL_LIBRARY(mod_rewrite mod_rewrite.c)
ADD_AND_INSTALL_LIBRARY(mod_auth "mod_auth.c;http_auth_digest.c;http_auth.c")
ADD_AND_INSTALL_LIBRARY(mod_sql_vhost_core mod_sql_vhost_core.c)
ADD_AND_INSTALL_LIBRARY(mod_mysql_vhost mod_mysql_vhost.c)
ADD_AND_INSTALL_LIBRARY(mod_trigger_b4_dl mod_trigger_b4_dl.c)
ADD_AND_INSTALL_LIBRARY(mod_uploadprogress mod_uploadprogress.c)
ADD_AND_INSTALL_LIBRARY(mod_evasive mod_evasive.c)
ADD_AND_INSTALL_LIBRARY(mod_ssi "mod_ssi_exprparser.c;mod_ssi_expr.c;mod_ssi.c")
ADD_AND_INSTALL_LIBRARY(mod_flv_streaming mod_flv_streaming.c)
ADD_AND_INSTALL_LIBRARY(mod_chunked mod_chunked.c)
ADD_AND_INSTALL_LIBRARY(mod_webdav mod_webdav.c)
ADD_AND_INSTALL_LIBRARY(mod_magnet "mod_magnet.c;mod_magnet_cache.c")

SET(L_INSTALL_TARGETS ${L_INSTALL_TARGETS} lighttpd)

IF(HAVE_PCRE_H)
  TARGET_LINK_LIBRARIES(mod_rewrite pcre)
  TARGET_LINK_LIBRARIES(mod_dirlisting pcre)
  TARGET_LINK_LIBRARIES(mod_redirect pcre)
  TARGET_LINK_LIBRARIES(mod_ssi pcre)
  TARGET_LINK_LIBRARIES(mod_trigger_b4_dl pcre)
ENDIF(HAVE_PCRE_H)

IF(HAVE_LUA_H)
  TARGET_LINK_LIBRARIES(mod_magnet lua m)
ENDIF(HAVE_LUA_H)

IF(HAVE_MYSQL_H AND HAVE_LIBMYSQL)
  TARGET_LINK_LIBRARIES(mod_mysql_vhost mysqlclient)
  INCLUDE_DIRECTORIES(/usr/include/mysql)
ENDIF(HAVE_MYSQL_H AND HAVE_LIBMYSQL)

SET(L_MOD_WEBDAV)
IF(HAVE_SQLITE3_H)
  SET(L_MOD_WEBDAV ${L_MOD_WEBDAV} sqlite3)
ENDIF(HAVE_SQLITE3_H)
IF(HAVE_LIBXML_H)
  SET(L_MOD_WEBDAV ${L_MOD_WEBDAV} xml)
ENDIF(HAVE_LIBXML_H)
IF(HAVE_UUID_H)
  SET(L_MOD_WEBDAV ${L_MOD_WEBDAV} uuid)
ENDIF(HAVE_UUID_H)

TARGET_LINK_LIBRARIES(mod_webdav ${L_MOD_WEBDAV})

SET(L_MOD_AUTH)
IF(HAVE_LIBCRYPT)
  SET(L_MOD_AUTH ${L_MOD_AUTH} crypt)
ENDIF(HAVE_LIBCRYPT)

IF(HAVE_LDAP_H)
  SET(L_MOD_AUTH ${L_MOD_AUTH} ldap lber)
ENDIF(HAVE_LDAP_H)
TARGET_LINK_LIBRARIES(mod_auth ${L_MOD_AUTH})

IF(HAVE_ZLIB_H)
  IF(HAVE_BZLIB_H)
    TARGET_LINK_LIBRARIES(mod_compress z bz2)
    TARGET_LINK_LIBRARIES(mod_deflate z bz2)
  ELSE(HAVE_BZLIB_H)
    TARGET_LINK_LIBRARIES(mod_compress z)
    TARGET_LINK_LIBRARIES(mod_deflate z)
  ENDIF(HAVE_BZLIB_H)
ENDIF(HAVE_ZLIB_H)

IF(CMAKE_COMPILER_IS_GNUCC)
  ##  SET_TARGET_PROPERTIES(lighttpd PROPERTIES COMPILE_FLAGS "-std=gnu99 -Wall -O2 -g -Wshadow -W")
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -O2 -g -Wshadow -W -pedantic")
  ADD_DEFINITIONS(-D_GNU_SOURCE)
ENDIF(CMAKE_COMPILER_IS_GNUCC)

IF(CMAKE_SYSTEM MATCHES "Linux")
  ## on linux we need pthread and librt for posix-aio
  SET_TARGET_PROPERTIES(lighttpd PROPERTIES LINK_FLAGS "${GTHREAD_LDFLAGS} -pthread -lrt")
ENDIF(CMAKE_SYSTEM MATCHES "Linux")

IF(CMAKE_SYSTEM MATCHES "FreeBSD")
  ## on FreeBSD we need pthread
  SET_TARGET_PROPERTIES(lighttpd PROPERTIES LINK_FLAGS "${GTHREAD_LDFLAGS} -pthread")
ENDIF(CMAKE_SYSTEM MATCHES "FreeBSD")

SET_TARGET_PROPERTIES(lighttpd PROPERTIES CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

IF(NOT BUILD_STATIC)
  IF(HAVE_LIBDL)
    TARGET_LINK_LIBRARIES(lighttpd dl)
  ENDIF(HAVE_LIBDL)
  ELSE(HAVE_LIBDL)
	  ## on FreeBSD
    TARGET_LINK_LIBRARIES(lighttpd c)
  ENDIF(HAVE_LIBDL)
ENDIF(NOT BUILD_STATIC)

IF(HAVE_LIBAIO_H)
  TARGET_LINK_LIBRARIES(lighttpd aio)
ENDIF(HAVE_LIBAIO_H)

IF(HAVE_PCRE_H)
  TARGET_LINK_LIBRARIES(lighttpd pcre)
ENDIF(HAVE_PCRE_H)

IF(HAVE_LIBSSL AND HAVE_LIBCRYPTO)
  TARGET_LINK_LIBRARIES(lighttpd ssl)
  TARGET_LINK_LIBRARIES(lighttpd crypto)
ENDIF(HAVE_LIBSSL AND HAVE_LIBCRYPTO)

INSTALL(TARGETS ${L_INSTALL_TARGETS}
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib/static)

