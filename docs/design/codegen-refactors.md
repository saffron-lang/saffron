# Codegen Antipattern Refactors

8 string-encoded-struct antipatterns found in the codegen audit. Each section is self-contained for handoff to a subagent.

---

## 1. `class_fields: Map<String, String>` stores `"name:Type,name2:Type2"`

**Priority:** HIGH (caused bugs this session)

### Current code

**Declaration** (`codegen.sf:26`):
```saffron
var class_fields: Map<String, String>
```

**Written** (`codegen/stmts_body.sf:318-319`, `codegen.sf:603`, `codegen.sf:1049`, etc.):
```saffron
gen.class_fields.set(cn3, cf3_all)          // cf3_all = "name:Type,name2:Type2"
this.class_fields.set(name, fields)          // fields already in string form
```

**Read** (`codegen/methods_body.sf:2199`, `:2232`):
```saffron
var fields: String = this.class_fields.get(__gft_lookup)
var parts: List<String> = this.split_respecting_generics(fields, ",")
var fp: List<String> = parts[i].split(":")   // fp[0] = name, fp[1] = type
```

Also used in `find_class_for_method` (`:12`) to distinguish interfaces (empty string) from concrete classes.

### Problem

- Field types containing commas or colons (generics like `Map<String,Int>`) require `split_respecting_generics` everywhere. One caller using plain `.split(",")` silently corrupts the index.
- `get_field_index` does a linear scan of a comma-joined string on every field access.
- The `class_field_list: Map<String, List<String>>` was already added (`codegen.sf:27`) as a parallel structure but is only used by `emit_reflect_helpers`. The two maps can drift.

### Target

Remove `class_fields` entirely. Use `class_field_list: Map<String, List<String>>` as the single source of truth. Each entry is `"name:Type"` (unchanged format per entry, just stored as list elements instead of comma-joined).

Optionally, promote further to `Map<String, List<AST.Param>>` to preserve the AST node, but that requires all readers to destructure `Param(n, t, d)`. The intermediate step (list of strings) is safer for bootstrap.

### Migration steps

1. **Add helper** `get_class_fields_str(name: String): String` that joins `class_field_list.get(name)` with commas. Replace all `.class_fields.get(X)` calls with this helper. Verify bootstrap passes.

2. **Change all writers** to write to `class_field_list` instead of (or in addition to) `class_fields`:
   - `codegen.sf` lines 591-604, 1037-1051, 1049-1051, 1112-1125, 1566-1579, 1637-1650
   - `codegen/stmts_body.sf` lines 318-322, 236-253

3. **Change all readers** to use `class_field_list` directly:
   - `get_field_type` (`methods_body.sf:2199`) — iterate list, split each entry on `:`, no outer split needed
   - `get_field_index` (`methods_body.sf:2232`) — same
   - `find_class_for_method` (`methods_body.sf:12`) — check `.length() == 0` on the list
   - `gen_class_decl` (`stmts_body.sf:326-337`) — already builds `field_entries` from parts
   - `gen_class_constructor` (`stmts_body.sf:481`) — receives `field_list: List<String>` already
   - `emit_reflect_helpers` (`stmts_body.sf:1313`) — already uses `class_field_list`
   - `gen_namespace_call` (`methods_body.sf:584`) — checks `class_fields.has(ns)`

4. **Remove `class_fields` field** from `Codegen` class and all initialization.

5. **Verify** bootstrap still passes.

### Dependencies

Does NOT require gen2 promotion. The refactoring uses only data structures gen2 already supports (List, Map, String splitting).

### Test

```bash
./bootstrap.sh
tools/saffron run test/classes.sf
tools/saffron run test/inheritance.sf
tools/saffron run test/reflect.sf
```

---

## 2. `params` string passed to `gen_function` as `"name:type,name:type"`

**Priority:** HIGH (caused bugs this session)

### Current code

**Signature** (`codegen/output_body.sf:1`):
```saffron
fun gen_function(name: String, params: String, ret_type: String, body: List<AST.Stmt>)
```

**Callers** (`codegen/stmts_body.sf:75`, `:478`):
```saffron
var params_str: String = this.params_to_string(params)  // AST.Param list → string
this.gen_function(name, params_str, ret_type, body)
```

**Internal parsing** (`output_body.sf:185-197`):
```saffron
var param_list: List<String> = []
if (params.length() > 0) {
    param_list = this.split_respecting_generics(params, ",")
}
// Then for each: parts[i].split(":") → [name, type]
```

### Problem

- Immediate re-parsing of something that was just serialized from `List<AST.Param>`. The round-trip loses default values (they are NOT encoded in the string).
- Generic param types like `Map<String,Int>` require `split_respecting_generics` instead of plain split, but `split(":")` on the entry ALSO breaks if the type contains `:` (unlikely now but fragile).
- Five different callers each call `params_to_string` then pass the result.

### Target

```saffron
fun gen_function(name: String, params: List<AST.Param>, ret_type: String, body: List<AST.Stmt>)
```

Remove all `params_to_string` calls at call sites. Inside `gen_function`, destructure `Param(n, t, d)` directly.

### Migration steps

1. **Create `gen_function_v2`** with `List<AST.Param>` signature. Implement by destructuring params inline (no string splitting). Have it call through to `gen_function` initially (bridge pattern) by calling `params_to_string` internally.

2. **Migrate callers one at a time** to pass `List<AST.Param>` directly:
   - `gen_stmt` FunDecl arm (`stmts_body.sf:75`) — already has `params: List<AST.Param>`
   - `gen_class_method` (`stmts_body.sf:478`) — builds `full_params` string from mparams
   - `gen_extend_method` (`methods_body.sf:462-469`) — same pattern
   - `gen_closure_function` (`closures_body.sf:186`) — receives params as string
   - Nested function path in `gen_function` itself (`output_body.sf:27-172`)

3. **Inline** `gen_function_v2` logic, remove string-based `gen_function`.

4. **Update `gen_class_method`** to build `List<AST.Param>` with a synthetic `Param("self", IntType, NilLit)` prepended instead of string concatenation.

5. **Update nested function path** (`output_body.sf:66-80`) to work with the list directly (captures prepended as synthetic `AST.Param` entries).

### Dependencies

Does NOT require promotion. `List<AST.Param>` is fully supported by gen2.

### Test

```bash
./bootstrap.sh
tools/saffron run test/functions.sf
tools/saffron run test/closures.sf
tools/saffron run test/classes.sf
```

---

## 3. `loop_end_label: String` stores `"end_label:cond_label"`

**Priority:** HIGH (caused bugs this session)

### Current code

**Declaration** (`codegen.sf:35`):
```saffron
var loop_end_label: String
```

**Written** (`codegen/stmts_body.sf:1185`):
```saffron
this.loop_end_label = end_label + ":" + cond_label
```

**Read** (`codegen/stmts_body.sf:16-23`):
```saffron
Break => {
    if (this.loop_end_label.contains(":")) {
        this.emit_terminator("br label %" + this.loop_end_label.split(":")[0])
    }
}
Continue => {
    if (this.loop_end_label.contains(":")) {
        this.emit_terminator("br label %" + this.loop_end_label.split(":")[1])
    }
}
```

### Problem

- If a label name ever contains `:` the split breaks. Label names are controlled (e.g. `while.end42`) so this hasn't happened yet, but it's gratuitously fragile.
- The `.contains(":")` check is used as a proxy for "are we inside a loop?" — a boolean would be clearer.
- Only two fields packed into one string; the overhead of splitting is unnecessary.

### Target

```saffron
var loop_break_label: String    // "" when not in a loop
var loop_continue_label: String // "" when not in a loop
```

### Migration steps

1. **Add two new fields** to `Codegen` class, initialize to `""`.

2. **Update `gen_while`** (`stmts_body.sf:1179-1215`):
   ```saffron
   var saved_break: String = this.loop_break_label
   var saved_continue: String = this.loop_continue_label
   this.loop_break_label = end_label
   this.loop_continue_label = cond_label
   // ... body ...
   this.loop_break_label = saved_break
   this.loop_continue_label = saved_continue
   ```

3. **Update Break/Continue** in `gen_stmt` (`stmts_body.sf:16-23`):
   ```saffron
   Break => {
       if (this.loop_break_label.length() > 0) {
           this.emit_terminator("br label %" + this.loop_break_label)
       }
   }
   Continue => {
       if (this.loop_continue_label.length() > 0) {
           this.emit_terminator("br label %" + this.loop_continue_label)
       }
   }
   ```

4. **Remove** `loop_end_label` field.

### Dependencies

Does NOT require promotion. Trivial field addition.

### Test

```bash
./bootstrap.sh
tools/saffron run test/pass/for_header_infer.sf
tools/saffron run test/pass/loop_control_valid.sf
```

---

## 4. `prefixes_joined` parameter is `"prefix1|prefix2|..."`

**Priority:** MEDIUM

### Current code

**Constructed** (`main.sf:734-740`):
```saffron
var prefixes_joined: String = ""
var mfi: Float = 0
while (mfi < module_stmts.length()) {
    if (mfi > 0) { prefixes_joined = prefixes_joined + "|" }
    prefixes_joined = prefixes_joined + module_prefixes_list[mfi]
    mfi = mfi + 1
}
```

**Function signatures** (`codegen.sf:899`, `:903`, `:1440`):
```saffron
fun generate_with_modules_flat(... prefixes_joined: String ...)
fun generate_with_modules_flat_opts(... prefixes_joined: String ...)
fun generate_with_modules_flat_opts2(... prefixes_joined: String ...)
```

**Parsed** inside each function (`codegen.sf:922`, `:1453`):
```saffron
var prefixes: List<String> = prefixes_joined.split("|")
```

### Problem

- Unnecessary serialization/deserialization: `main.sf` already has `module_prefixes_list: List<String>` and joins it only because "native list issues" were feared (comment on line 731). Those issues were fixed when gen2 was promoted.
- If a module prefix contains `|` (impossible today but fragile).
- Three function variants each duplicate the split logic.

### Target

Pass `List<String>` directly:
```saffron
fun generate_with_modules_flat_opts(... prefixes: List<String> ...)
```

### Migration steps

1. **Add new function** `generate_with_modules_flat_v2` that accepts `List<String>`.

2. **Have old function** delegate: `var prefixes = prefixes_joined.split("|")` then call v2. This preserves bootstrap compat.

3. **Update `main.sf`** to call v2 directly with `module_prefixes_list`.

4. **Remove** old functions after bootstrap verifies.

### Dependencies

Does NOT require promotion. List passing works in gen2.

### Test

```bash
./bootstrap.sh
tools/saffron run test/imports.sf
tools/saffron run test/modules.sf
```

---

## 5. `builtin_methods` stores `"v2|__list_push|Nil|0"`

**Priority:** MEDIUM

### Current code

**Declaration** (`codegen.sf:52`):
```saffron
var builtin_methods: Map<String, String>
```

**Written** (`codegen.sf:120-136`):
```saffron
this.builtin_methods.set("push", "v2|__list_push|Nil|0")
this.builtin_methods.set("pop", "r1|__list_pop|Int|0")
// ... 14 more entries
```

**Read** (`codegen/methods_body.sf:484-489`):
```saffron
var desc: String = this.builtin_methods.get(method)
var parts: List<String> = desc.split("|")
var pattern: String = parts[0]
var func_name: String = parts[1]
var result_type: String = parts[2]
var needs_push: String = parts[3]
```

### Problem

- Positional encoding: adding a 5th field requires updating every entry and every reader.
- The "pattern" field (v2, r1, r2, p2, p3, m2) encodes calling convention cryptically.
- Error messages are meaningless if a split produces the wrong count.
- Can't introspect or extend at runtime.

### Target

Use a struct-like encoding with a dedicated map. Since Saffron doesn't have anonymous structs in the compiler source (gen2 limitation), use a `Map<String, List<String>>` where each list is `[pattern, func_name, result_type, needs_push]`:

```saffron
var builtin_methods: Map<String, List<String>>
```

Or, cleaner: four parallel maps:
```saffron
var builtin_pattern: Map<String, String>
var builtin_func: Map<String, String>
var builtin_ret: Map<String, String>
var builtin_needs_push: Map<String, Bool>
```

The parallel-maps approach avoids list indexing and makes each lookup explicit.

### Migration steps

1. **Add four parallel maps** alongside existing `builtin_methods`.

2. **Add a helper** `register_builtin(method, pattern, func, ret, needs_push)` that writes to all four maps AND the legacy string map.

3. **Replace** all 16 `.set(name, "...")` calls with `register_builtin(...)`.

4. **Update `emit_builtin_dispatch`** to read from the four maps instead of splitting.

5. **Remove** the legacy `builtin_methods` map.

### Dependencies

Does NOT require promotion. Maps and booleans are gen2-compatible.

### Test

```bash
./bootstrap.sh
tools/saffron run test/lists.sf
tools/saffron run test/strings.sf
tools/saffron run test/maps.sf
```

---

## 6. `enum_variant_fields: Map<String, String>` stores `"name:Type,name2:Type2"`

**Priority:** MEDIUM (same fix as #1)

### Current code

**Declaration** (`codegen.sf:22`):
```saffron
var enum_variant_fields: Map<String, String>
```

**Written** (`codegen/stmts_body.sf:550`, `codegen.sf:770`, etc.):
```saffron
this.enum_variant_fields.set(name + "." + vname, vfields)
// vfields comes from AST.Variant: match (v) { Variant(n, f) => f }
// f is already a "name:Type,name2:Type2" string from the parser
```

**Read** (`codegen/stmts_body.sf:652`, match codegen, `get_variant_field_type`):
```saffron
var parts: List<String> = this.split_respecting_generics(vfields, ",")
var field_part: String = parts[fi]
var ft_parts: List<String> = field_part.split(":")
```

### Problem

Same as #1: generic types with commas require `split_respecting_generics`. The string comes from `AST.Variant`'s fields string, which the parser already encoded. Re-parsing at every use is wasteful and fragile.

### Target

```saffron
var enum_variant_fields: Map<String, List<String>>
```

Each list entry is `"name:Type"`. Same intermediate representation as `class_field_list`.

Longer-term: change `AST.Variant` to store `List<AST.Param>` instead of a string.

### Migration steps

1. **Change** `get_variant_fields(v)` to return a `List<String>` (split at point of extraction from AST).

2. **Change** the map value type and all writers (register_variant, pre-registration loops in `codegen.sf`).

3. **Change** all readers (enum constructors, match arm extraction, `get_variant_field_type`).

4. **Bootstrap** and verify.

### Dependencies

Depends on whether `AST.Variant` field format changes. If we keep AST.Variant as `Variant(name: String, fields: String)`, the split happens once at registration time. Does NOT require gen2 promotion.

### Test

```bash
./bootstrap.sh
tools/saffron run test/enums.sf
tools/saffron run test/match.sf
tools/saffron run test/option.sf
```

---

## 7. Import/extend info smuggled through VarDecl/FunDecl `doc` field

**Priority:** ARCHITECTURAL DEBT

### Current code

**Import smuggling** — parser emits VarDecl with doc `"@import:path|as:Alias"`:
- Checked in `stmts_body.sf:5`: `if (!doc.starts_with("@import:")) { ... }`
- Checked in `codegen.sf:539`, `:982`: `if (!vdoc2.starts_with("@import:")) { ... }`
- `emit_block_alloca_for` (`stmts_body.sf:879`): skip if doc starts with `@import:`

**Extend smuggling** — parser emits FunDecl with doc `"@extend:ClassName"`:
- Checked in `stmts_body.sf:50-56`: `if (doc.starts_with("@extend:")) { ... }`
- Pre-scanned in `codegen.sf:558-566`, `:1002-1009`

**Intrinsic/extern smuggling** — doc holds `"@intrinsic"` or `"@extern:sig"`:
- `stmts_body.sf:39-49`

### Problem

- The `doc` field is conceptually a docstring, not a control channel. Using it as metadata conflates two concerns.
- Pattern matching must exhaustively check doc prefixes in multiple places. Easy to miss a new prefix.
- Import VarDecls exist solely as metadata carriers — they generate no code. They pollute the statement list and require special-case filtering everywhere (`is_decl`, `classify_stmt`, alloca emission, etc.).

### Target

Add dedicated AST nodes:
```saffron
enum Stmt {
    // ... existing ...
    ImportDecl(path: String, alias: String, named: List<String>)
    ExtendDecl(class_name: String, method: AST.Stmt)  // wraps FunDecl
}
```

Or, simpler intermediate step: add `kind` field to FunDecl/VarDecl that is an enum:
```saffron
enum DeclKind { Normal, Import, Extend, Intrinsic, Extern }
```

### Migration steps

1. **Phase 1 (no AST change):** Extract all `doc.starts_with("@...")` checks into a single `classify_decl_doc(doc)` helper that returns a discriminator string. This consolidates the branching.

2. **Phase 2 (AST change):** Add `ImportDecl` node to `AST.Stmt`. Update parser to emit it instead of synthetic VarDecl. Update codegen to handle it in `gen_stmt` match. Update checker.

3. **Phase 3:** Add `ExtendDecl` wrapper. Remove `pending_extend_class` field and the pre-scan extend map (the information lives in the AST node directly).

4. **Phase 4:** Move `@intrinsic`/`@extern:` to a `FunKind` discriminator on FunDecl or separate nodes.

### Dependencies

**Requires gen2 promotion** after Phase 2+ (new enum variants in AST.Stmt). Phase 1 does not.

### Test

```bash
./bootstrap.sh
tools/saffron run test/imports.sf
tools/saffron run test/extend_fun.sf
tools/saffron run test/extern.sf
```

---

## 8. `func_prefix_map["__caps_" + name]` stores `"var1,var2,var3"`

**Priority:** ARCHITECTURAL DEBT

### Current code

**Written** (`codegen/output_body.sf:83-84`):
```saffron
this.called_function_arity.set("__captures_" + emit_name, captures.length())
this.func_prefix_map.set("__caps_" + emit_name, captures.join(","))
```

**Read** (`codegen/closures_body.sf:427-441`):
```saffron
var caps_key: String = "__caps_" + this.current_prefix + call_name
if (this.func_prefix_map.has(caps_key)) {
    var caps_str: String = this.func_prefix_map.get(caps_key)
    if (caps_str.length() > 0) {
        var cap_list: List<String> = caps_str.split(",")
        // iterate cap_list to propagate free vars
    }
}
```

Also read in expression codegen (`expr_body.sf:1585-1586`):
```saffron
if (this.called_function_arity.has("__captures_" + resolved_callee)) {
    var caps_str: String = this.func_prefix_map.get("__caps_" + resolved_callee)
    // split and iterate
}
```

### Problem

- `func_prefix_map` is overloaded: it stores BOTH legitimate function name mappings (`name → prefixed_name`) AND capture lists (keyed with `"__caps_"` prefix). These are conceptually different data.
- Variable names containing commas would break (impossible in practice but still a code smell).
- The `"__captures_"` key in `called_function_arity` is a separate namespace hack on the same map.
- Readers must always re-split; no way to inspect captures as a structured list.

### Target

Add a dedicated field:
```saffron
var captures_map: Map<String, List<String>>
```

### Migration steps

1. **Add `captures_map` field** to `Codegen`, initialize to `{}`.

2. **Update writer** (`output_body.sf:84`):
   ```saffron
   this.captures_map.set(emit_name, captures)
   ```

3. **Update readers**:
   - `closures_body.sf:427-441`: `if (this.captures_map.has(key)) { var cap_list = this.captures_map.get(key) }`
   - `expr_body.sf:1585-1586`: same pattern

4. **Keep `called_function_arity["__captures_" + name]`** as-is for now (stores capture count, not list), or migrate to `this.captures_map.get(name).length()`.

5. **Remove** all `"__caps_"` entries from `func_prefix_map`.

### Dependencies

Does NOT require promotion. `Map<String, List<String>>` is gen2-compatible.

### Test

```bash
./bootstrap.sh
tools/saffron run test/closures.sf
tools/saffron run test/nested_functions.sf
tools/saffron run test/captures.sf
```

---

## Execution Order

Recommended order to minimize risk and maximize immediate benefit:

1. **#3** (loop labels) — smallest change, zero risk, immediate clarity gain
2. **#8** (captures map) — small, isolated, removes namespace pollution from func_prefix_map
3. **#1** (class_fields) — high impact but larger surface area; `class_field_list` already exists
4. **#2** (gen_function params) — high impact, moderate refactor surface
5. **#5** (builtin_methods) — medium scope, improves extensibility
6. **#6** (enum_variant_fields) — same pattern as #1, can reuse approach
7. **#4** (prefixes_joined) — small change but touches function signatures across files
8. **#7** (AST node smuggling) — requires promotion cycle, do last

Each refactor should be a single commit with its own bootstrap verification.

---

## 9. NaN-Boxing via LLVM Library Refactor

**Priority:** HIGH (blocks println(Any), runtime type dispatch, full module-as-object)

### Current State

The codegen emits raw values everywhere:
- IntLit(42) emits add i64 0, 42 (no tag)
- BoolLit(true) emits add i64 0, 1 (no tag)  
- StringLit("hi") emits ptrtoint i8* ... to i64 (no tag)
- NilLit emits literal "0" (not the NaN-boxed nil constant)

Tag/untag calls exist only at boundaries (IO printing, extern calls). The system assumes they are no-ops.

### Target State

All value creation goes through the NanBox helper in src/lib/llvm/nanbox.sf.
The LLVM lib provides typed tag/untag operations via BlockBuilder + NanBox class.
NaN-boxing becomes just a link-time choice: base.ll (identity) vs base_nanbox.ll (real).

### Why This Is Part of the LLVM Lib Refactor

When the codegen uses LLVM.BlockBuilder instead of string concatenation:
1. Every emit_indent("add i64 0, " + val) becomes bb.add(...)
2. NanBox wrapper fits naturally: nb.tag_int(bb.const_int(42))
3. The same codegen produces both modes by choosing which base to link
4. src/lib/llvm/nanbox.sf already has the NanBox class with tag_int, tag_ptr, untag_int, etc.

### Migration Steps (done as part of LLVM lib codegen rewrite)

1. Rewrite literal emission to use NanBox (tag_int, tag_ptr, tag_bool, nil_val)
2. Rewrite arithmetic to untag/compute/retag
3. Fix intrinsics (load64/store64 untag addresses)
4. Compile runtime without identity-mode (or keep boundary via extern void*)
5. Complete base_nanbox.ll (copy missing symbols from base.ll)
6. Add --nanbox flag to tools/saffron driver

### Dependencies

- Requires LLVM lib codegen rewrite
- Does NOT require gen2 promotion
- Does NOT block anything else (identity-mode continues working)
