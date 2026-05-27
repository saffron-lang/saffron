# Saffron TODO

## Type System
- [ ] Disallow bare generics: `List`, `Map` must be `List<T>`, `Map<K,V>` — type error otherwise
- [ ] obj_type in codegen dispatch should be AST.Type nodes, not strings
- [ ] All type comparisons should use pattern matching on AST.Type, not string == / starts_with
- [ ] runtime.ll should eventually be a .sf file compiled by the compiler itself

## Native Binary (Gen1) Fixes
- [ ] Multi-param functions show params.length=0 in native binary
- [ ] Class method dispatch fails (var_types lookup returns wrong type for class instances)
- [ ] Root cause: native binary's var_types map doesn't reliably store/retrieve type strings
- [ ] Method-name-first dispatch (no overloading in compiler) would bypass var_types issue
