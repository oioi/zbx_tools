project(llid)
set (SOURCES main.cpp)
add_executable(llid ${SOURCES})
target_link_libraries(llid liblog.a libbuffer.a netsnmp)
