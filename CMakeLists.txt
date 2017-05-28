cmake_minimum_required(VERSION 3.7)
project(kilo)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES src/kilo.c src/append_buffer.h src/append_buffer.c)
add_executable(kilo ${SOURCE_FILES})
