project(sba-disc)
set (HEADERS data.h)
set (SOURCES screen.cpp main.cpp)

add_executable(sba-disc ${SOURCES} ${HEADERS})
target_link_libraries(sba-disc
                      liblog.a
		      libbuffer.a
                      libconfig.a
                      libsnmp.a
                      libbasic_mysql.a
                      libbasic_curl.a
                      libzbxapi.a
                      libfrozen.a

                      confuse
                      netsnmp
                      boost_regex
                      mysqlclient
                      curl
)
