cmake_minimum_required(VERSION 3.25)
project(crafting_interpreters C)

set(CMAKE_C_STANDARD 17)

add_executable(crafting_interpreters src/main.c src/common.h src/chunk.h src/chunk.c src/memory.h src/memory.c src/debug.h src/debug.c src/value.h src/value.c src/vm.h src/vm.c src/compiler.c src/compiler.h src/scanner.h src/scanner.c src/object.c src/object.h src/table.c src/table.h)
