cmake_minimum_required(VERSION 3.7)
project(pound)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES src/pound.c src/append_buffer.h src/append_buffer.c src/terminal.c src/terminal.h src/row.c src/row.h)
add_executable(pound ${SOURCE_FILES})
