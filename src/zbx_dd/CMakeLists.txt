project(zbx_dd)
set (SOURCES create.cpp main.cpp snmp.cpp trigdepend.cpp update.cpp)
set (HEADERS create.h main.h snmp.h trigdepend.h update.h)

add_executable(zbx_dd ${SOURCES} ${HEADERS})
target_link_libraries(zbx_dd
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
