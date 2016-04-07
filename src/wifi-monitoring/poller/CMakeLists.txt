project(wifi-poller)
set (HEADERS md5.h MikrotikAPI.h MikrotikAPITypes.h)
set (SOURCES md5.c MikrotikAPI.cpp MikrotikAPITypes.cpp main.cpp)

add_executable(wifi-poller ${SOURCES} ${HEADERS})
target_link_libraries(wifi-poller
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
