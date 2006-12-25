SET(T_FILES
	cachable.t
	core-condition.t
	core-keepalive.t
	core-request.t
	core-response.t
	core.t
	core-var-include.t
	lowercase.t
	mod-access.t
	mod-auth.t
	mod-cgi.t
	mod-compress.t
	mod-fastcgi.t
	mod-proxy.t
	mod-redirect.t
	mod-rewrite.t
	mod-secdownload.t
	mod-setenv.t
	mod-ssi.t
	mod-userdir.t
	request.t
)

FOREACH(it ${T_FILES})
  ADD_TEST(${it} ${lighttpd_SOURCE_DIR}/tests/${it})
ENDFOREACH(it)
