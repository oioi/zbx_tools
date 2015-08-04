bin_PROGRAMS = zbx_mail
zbx_mail_SOURCES = mail.h mail.cpp main.h main.cpp
zbx_mail_LDADD = $(top_srcdir)/src/libs/buffer/libbuffer.a \
		 $(top_srcdir)/src/libs/config/libconfig.a \
		 $(top_srcdir)/src/libs/logging/liblogging.a \
		 $(top_srcdir)/src/libs/curl_cl/libbasic_curl.a \
		 $(top_srcdir)/src/libs/api/libzbx_api.a \
		 $(top_srcdir)/src/libs/frozen/libfrozen.a
zbx_mail_LDFLAGS = -lcurl -lconfuse -lboost_regex -lssl
