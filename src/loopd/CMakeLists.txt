project(loopd)
set (HEADERS main.h device.h lrrd.h)
set (SOURCES main.cpp device.cpp lrrd.cpp)

add_executable(loopd ${SOURCES} ${HEADERS})
target_link_libraries(loopd
                      liblog.a   
                      libbuffer.a
                      libconfig.a
                      libbasic_curl.a
                      libzbxapi.a
                      libfrozen.a
                      libsnmp.a

                      netsnmp
                      confuse
                      curl
                      rrd
)
