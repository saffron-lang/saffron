cmake_minimum_required(VERSION 3.25)
project(crafting_interpreters C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_OSX_ARCHITECTURES "arm64")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")

add_executable(crafting_interpreters src/main.c src/common.h src/chunk.h src/chunk.c src/memory.h src/memory.c src/debug.h src/debug.c src/value.h src/value.c src/vm.h src/vm.c src/compiler.c src/compiler.h src/scanner.h src/scanner.c src/object.c src/object.h src/table.c src/table.h src/libc/list.c src/libc/list.h src/libc/map.c src/libc/map.h src/libc/time.c src/libc/time.h src/libc/type.c src/libc/type.h src/libc/io.c src/libc/io.h src/libc/async.c src/libc/async.h src/libc/task.c src/libc/task.h)
