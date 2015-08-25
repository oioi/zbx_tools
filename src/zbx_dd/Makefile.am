bin_PROGRAMS = zbx_dd
zbx_dd_SOURCES = snmp.h snmp.cpp create.h create.cpp update.h update.cpp main.h main.cpp
zbx_dd_LDADD = $(top_srcdir)/src/libs/buffer/libbuffer.a \
		 $(top_srcdir)/src/libs/config/libconfig.a \
		 $(top_srcdir)/src/libs/logging/liblogging.a \
		 $(top_srcdir)/src/libs/curl_cl/libbasic_curl.a \
		 $(top_srcdir)/src/libs/api/libzbx_api.a \
		 $(top_srcdir)/src/libs/frozen/libfrozen.a \
		 $(top_srcdir)/src/libs/mysql/libbasic_mysql.a
zbx_dd_LDFLAGS = -lcurl -lconfuse -lnetsnmp -lmysqlclient
