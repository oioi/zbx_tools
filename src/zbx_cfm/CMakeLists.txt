project(zbx_cfm)
set (SOURCES main.cpp)
set (HEADERS main.h)

add_executable(zbx_cfm ${SOURCES} ${HEADERS})
target_link_libraries(zbx_cfm 
                      libbuffer.a 
                      libconfig.a 
                      liblog.a 
                      libbasic_curl.a 
                      libzbxapi.a 
                      libfrozen.a 
                      libbasic_mysql.a

		      curl
		      confuse
	              netsnmp
	              mysqlclient
)
