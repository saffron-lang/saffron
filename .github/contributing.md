## Contributing Workflow

1. Syntax
    - `scanner.c` for new tokens
    - `generate_ast.py` for new nodes
    - `astparse.c` for parsing into those new nodes
    - `types.c` for typechecking
2. Compilation
    - `chunk.h` for new OPs
    - `astcompile.c` to generate bytecode
3. VM
    - `vm.c` handle the new ops
    - `debug.c` debug printing for the new op when printing bytecode
4. Standard Library
    - `lib/` an importable lib file written in lox, types are automatic
    - `libc/` for a builtin library
        - Types
            - `types.c` inside `initGlobalEnvironment()` define any new types or values
            - `defineTypeDef()` for a top level builtin type
            - `defineLocal()` for a top level bultin value/function
            - New classes will need to define both the Type and the Local if the type is to be used for both annotations and as a callable
        - Implementation
            - `vm.c` inside `initVM()` define any new builtin functions, values, or modules
            - `defineType()` for any top level types to keep them from being garbage collected that you don't want also to be defined as values
            - `defineModule()` to create a new module in C, see the time module created in `time.c`
            - `defineBuiltin()` to define a builtin function or value at the top level, e.g. `list` if you want a callable list type.
            - `defineNative()` can define a function as a builtin and automatically wrap it in a Lox type
            - Any definition will keep it from being garbage collected