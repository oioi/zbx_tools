project(zbx_mail)
set (SOURCES mail.cpp main.cpp)
set (HEADERS mail.h main.h)

add_executable(zbx_mail ${SOURCES} ${HEADERS})
target_link_libraries(zbx_mail
                      libbuffer.a 
                      libconfig.a 
                      liblog.a 
                      libbasic_curl.a 
                      libzbxapi.a 
                      libfrozen.a 

		      curl
		      confuse
		      boost_regex
	              ssl
)
