# Saffron TODO

## Type System
- [ ] Disallow bare generics: `List`, `Map` must be `List<T>`, `Map<K,V>` — type error otherwise
- [ ] obj_type in codegen dispatch should be AST.Type nodes, not strings
- [ ] All type comparisons should use pattern matching on AST.Type, not string == / starts_with
- [ ] runtime.ll should eventually be a .sf file compiled by the compiler itself

## Parser
- [ ] Refactor match_kind_check/match_kind to compare TokenKind enum values directly instead of string conversion (130 call sites)

## Native Binary Fixes
- [ ] `this.field = value` crashes in gen2-compiled user programs (field GET works, field SET segfaults)
- [ ] Root cause under investigation: struct layout or GEP index mismatch in gen2 output vs C VM output
