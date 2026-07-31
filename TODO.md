# Saffron TODO

## Unified Dispatch (in progress)
- [x] gen_namespace_call helper for unified obj.method() dispatch
- [x] resolve_namespace for determining obj's namespace
- [x] Class method fallback uses gen_namespace_call
- [ ] IO/OS as proper modules (src/lib/io.sf, os.sf) — need println to call .to_string() on arg
- [ ] Remove hardcoded IO/OS dispatch from gen_method_call
- [ ] Remove module prefix mangling (use import alias as namespace name)
- [ ] Migrate this-dispatch to use gen_namespace_call

## Import System (in progress)
- [x] Parser produces @import: VarDecl metadata
- [x] AST-based import extraction in main.sf
- [ ] Named imports: `import { X, Y } from "path"` activates specific symbols
- [ ] Import-scoped extension methods (only visible where imported)
- [ ] Fix gen2 promotion crash (signal 137 when new gen3 used as gen2)

## Codegen Split (blocked on imports)
The `extend fun` mirror files (codegen/{expr,methods,utils,...}.sf) were deleted:
all 136 functions they shared with the active `*_body.sf` files had drifted, none
was still identical, and no function existed only in a mirror. Recover them from
git history (before the deletion commit) if this work resumes — but re-splitting
from the current `*_body.sf` files is likely cheaper than reviving 6.5k stale
lines. Real prerequisite is still the import system, per compiler-rewrite.md I11.
- [ ] Split gen_method_call out of codegen.sf via extend fun (needs working imports)
- [ ] Split expr codegen out via extend fun
- [ ] Split stmt codegen out via extend fun
- [ ] Then delete the sed assembly in bootstrap.sh (compiler-rewrite.md I11)

## Type System
- [ ] Disallow bare generics: `List`, `Map` must be `List<T>`, `Map<K,V>`
- [ ] obj_type in codegen dispatch should be AST.Type nodes, not strings
- [ ] All type comparisons should use pattern matching on AST.Type, not string == / starts_with

## Parser
- [ ] Refactor match_kind_check/match_kind to compare TokenKind enum values directly (130 call sites)

## Native Binary Fixes
- [x] Field-set crash was actually method name collision (get/set hijacked by builtins) — FIXED
- [ ] Gen2 promotion crash: new gen3 binary crashes with signal 137 when used as gen2
