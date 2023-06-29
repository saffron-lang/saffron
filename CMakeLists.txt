cmake_minimum_required(VERSION 3.25)
project(crafting_interpreters C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_OSX_ARCHITECTURES "arm64")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")

file(COPY src/lib DESTINATION .)

add_executable(crafting_interpreters
        src/main.c
        src/common.h
        src/chunk.h
        src/chunk.c src/memory.h src/memory.c src/debug.h src/debug.c src/value.h src/value.c src/vm.h src/vm.c
        src/ast/astcompile.c src/ast/astcompile.h
        src/scanner.h src/scanner.c
        src/object.c src/object.h src/table.c src/table.h src/libc/list.c src/libc/list.h src/libc/map.c src/libc/map.h src/libc/time.c src/libc/time.h src/libc/type.c src/libc/type.h src/libc/io.c src/libc/io.h src/libc/async.c src/libc/async.h src/libc/task.c src/libc/task.h src/libc/module.c src/libc/module.h src/files.h src/files.c src/ast/ast.c src/ast/ast.h src/ast/astprint.c src/ast/astprint.h src/ast/astparse.c src/ast/astparse.h src/types.c src/types.h src/valuetable.c src/valuetable.h src/libc/builtins.c src/libc/builtins.h)
