project(cisco-tempdisc)
set (SOURCES main.cpp)
add_executable(cisco-tempdisc ${SOURCES})
target_link_libraries(cisco-tempdisc liblog.a libbuffer.a libsnmp.a netsnmp)
