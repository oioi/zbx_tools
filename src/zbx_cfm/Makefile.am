bin_PROGRAMS = zbx_cfm
zbx_cfm_SOURCES = main.h main.cpp
zbx_cfm_LDADD = $(top_srcdir)/src/libs/buffer/libbuffer.a \
		 $(top_srcdir)/src/libs/config/libconfig.a \
		 $(top_srcdir)/src/libs/logging/liblogging.a \
		 $(top_srcdir)/src/libs/curl_cl/libbasic_curl.a \
		 $(top_srcdir)/src/libs/api/libzbx_api.a \
		 $(top_srcdir)/src/libs/frozen/libfrozen.a
zbx_cfm_LDFLAGS = -lcurl -lconfuse -lnetsnmp
