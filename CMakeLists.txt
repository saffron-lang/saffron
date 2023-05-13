cmake_minimum_required(VERSION 3.25)
project(crafting_interpreters C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_OSX_ARCHITECTURES "arm64")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")

add_executable(crafting_interpreters src/main.c src/common.h src/chunk.h src/chunk.c src/memory.h src/memory.c src/debug.h src/debug.c src/value.h src/value.c src/vm.h src/vm.c src/compiler.c src/compiler.h src/scanner.h src/scanner.c src/object.c src/object.h src/table.c src/table.h src/lib/list.c src/lib/list.h src/lib/map.c src/lib/map.h src/lib/time.c src/lib/time.h src/lib/type.c src/lib/type.h src/lib/io.c src/lib/io.h)
