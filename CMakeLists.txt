cmake_minimum_required(VERSION 3.31)
project(cdoku C)

set(CMAKE_C_STANDARD 99)

if(USE_MMAP)
    add_definitions(-DMMAP)
endif()

add_library(cdoku SHARED library.c)

add_executable(test_runner main.c)

target_link_libraries(test_runner cdoku)