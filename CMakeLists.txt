cmake_minimum_required(VERSION 2.6)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

if(UNIX)
    set(CMAKE_C_FLAGS "-Wall -pedantic  -std=c99")
    set(CMAKE_CXX_FLAGS "-Wall -pedantic")
endif(UNIX)

find_package(Readline REQUIRED)

include_directories(${Readline_INCLUDE_DIR})

add_definitions(-D_GNU_SOURCE)

set(SRC_FILES rline.c exsh.c)

if(UNIX)
    add_executable(exsh ${SRC_FILES} exsh.c rline.c)
    target_link_libraries(exsh ${Readline_LIBRARY})
endif(UNIX)
