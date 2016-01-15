project(hotspotm)
set (HEADERS md5.h MikrotikAPI.h MikrotikAPITypes.h main.h)
set (SOURCES md5.c MikrotikAPI.cpp MikrotikAPITypes.cpp main.cpp)

add_executable(hotspotm ${SOURCES} ${HEADERS})
target_link_libraries(hotspotm
		      libsqlite_db.a
                      liblog.a
		      libbuffer.a
                      libconfig.a
                      libzbx_sender.a
                      libfrozen.a

                      confuse
       		      sqlite3
                      curl
)
