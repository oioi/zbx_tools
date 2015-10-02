cmake_minimum_required(VERSION 2.8)
project(zbx_tools)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic --std=c++11")
include_directories(${zbx_tools_SOURCE_DIR}/include)


add_subdirectory(src)
