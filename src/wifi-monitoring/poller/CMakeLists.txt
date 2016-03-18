project(poller)
set (HEADERS md5.h MikrotikAPI.h MikrotikAPITypes.h)
set (SOURCES md5.c MikrotikAPI.cpp MikrotikAPITypes.cpp main.cpp)

add_executable(poller ${SOURCES} ${HEADERS})
target_link_libraries(poller
                      liblog.a
		      libbuffer.a
                      libconfig.a
                      libzbxapi.a
                      libfrozen.a
                      libzbx_sender.a
                      libbasic_curl.a

                      confuse
                      curl
                      boost_regex
)
