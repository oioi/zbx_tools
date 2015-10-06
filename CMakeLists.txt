cmake_minimum_required(VERSION 2.8)
project(zbx_tools)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic --std=c++11")
include_directories(${zbx_tools_SOURCE_DIR}/include)
link_directories(${zbx_tools_SOURCE_DIR}/src/lib)

add_subdirectory(src)
