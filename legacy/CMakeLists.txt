cmake_minimum_required(VERSION 3.25)
project(saffron C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_OSX_ARCHITECTURES "arm64")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")

file(COPY ../src/lib DESTINATION .)

add_executable(saffron
        main.c
        common.h
        chunk.h
        chunk.c memory.h memory.c debug.h debug.c value.h value.c vm.h vm.c
        ast/astcompile.c ast/astcompile.h
        scanner.h scanner.c
        object.c object.h table.c table.h libc/list.c libc/list.h libc/map.c libc/map.h libc/time.c libc/time.h libc/type.c libc/type.h libc/io.c libc/io.h libc/async.c libc/async.h libc/task.c libc/task.h libc/module.c libc/module.h files.h files.c ast/ast.c ast/ast.h ast/astprint.c ast/astprint.h ast/astparse.c ast/astparse.h types.c types.h valuetable.c valuetable.h libc/builtins.c libc/builtins.h libc/json.c libc/json.h libc/string.c libc/string.h libc/reflect.c libc/reflect.h libc/math_mod.c libc/math_mod.h libc/random.c libc/random.h libc/os.c libc/os.h libc/stringbuilder.c libc/stringbuilder.h libc/regex.c libc/regex.h libc/channel.c libc/channel.h diagnostic.c diagnostic.h)

# 64MB stack for deep recursive compilation of large Saffron programs
if(APPLE)
    target_link_options(saffron PRIVATE "LINKER:-stack_size,0x4000000")
endif()
