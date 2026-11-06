cmake_minimum_required(VERSION 3.31)
project(cdoku C)

set(CMAKE_C_COMPILER "gcc")

set(CMAKE_C_STANDARD 99)

add_executable(cdoku main.c)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
