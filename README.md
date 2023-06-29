# Saffron

A small statically typed scripting language

## Todo

- Static types and typechecking
- Pattern matching
- Imports
  - Discover files from import paths
  - Good semantics for relative imports
    - May not go beyond root of package
  - Packages
  - Write bytecode to file
    - Import bytecode file
    - Write multiple bytecode files to a zipped package
- Discover files for import
- List methods and accessing
- ~~Map~~ and linked list types
- Async networking
- File I/O
- Maybe something to do with currying?
- Statements as expressions
  - Allowing implicit return as the last stmt of a function
  - Typechecking on if expressions?
- Int and Float separation
  - Both as Long / Doubles?
  - Arbitrary size ints?
- Priority queue for async queuing
- Exception handling
  - Only during runtime we can determine where to jump in the event of an error
  - Add try, catch but still encourage returning errors
  - Different handlers for different errors
  - Panic!
- More complex typechecking that allows for referencing functions that are defined later