project(loopd)
set (HEADERS device.h data.h worker.h lrrd.h)
set (SOURCES device.cpp worker.cpp lrrd.cpp data.cpp main.cpp)

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
