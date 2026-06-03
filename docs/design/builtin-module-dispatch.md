# Builtin Module Dispatch Refactor

## Current State (gen_method_call lines 680-820)

```
if (obj_name == "OS") {
    if (method == "exit") { ... special code ... }
    if (method == "file_exists" or method == "mkdir" ...) { call __io_METHOD }
    call __os_METHOD  // fallback
}
if (obj_name == "IO") {
    if (method == "println") { ... coerce_to_string + call __io_println_str ... }
    if (method == "print") { ... coerce_to_string + call __io_print_str ... }
    if (method == "read_file" or ...) { call __io_METHOD }
}
if (obj_name == "GC") {
    if (method == "collect") { call __gc_collect }
    if (method == "enable") { call __gc_enable }
    ... 8 more methods ...
}
```

## Target State

```
// All builtin modules use a unified dispatch:
gen_builtin_module_call(module_name, method, args)

// Which resolves to:
// 1. Check if method needs special handling (println coercion, exit truncation)
// 2. Otherwise: call @__MODULE_METHOD(args)
```

## Migration Plan

### Step 1: Extract dispatch table
Create a map: `builtin_module_funcs` that maps `"IO.println"` → `{func: "__io_println_str", ret: "Nil", special: "coerce_str"}`

### Step 2: Replace if-chains with table lookup
```saffron
fun gen_builtin_module_call(module: String, method: String, args: List<AST.Expr>): String {
    var key = module + "." + method
    // ... table lookup and dispatch ...
}
```

### Step 3: Move to real module imports
Once the dispatch is table-driven, the table can be populated FROM the module's type descriptor instead of hardcoded.
