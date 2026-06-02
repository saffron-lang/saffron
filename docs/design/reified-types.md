# Reified Types — Type-Safe Deserialization for Saffron

## 1. Motivation

Saffron's current deserialization story has a significant ergonomics gap. Parsing TOML or JSON produces `Map<String, Any>`, forcing users into verbose, untyped access patterns:

```saffron
import "@toml" as TOML

var config = TOML.parse_file("pantry.toml")
var pkg = config.get("package")              // Any — no type info
var name: String = pkg.get("name")           // runtime cast, no compile-time guarantee
var authors = pkg.get("authors")             // List<Any> — lost element types
```

This has several problems:

- **No compile-time verification** — typos in key names produce runtime errors, not compile errors
- **No IDE completion** — `config.get("...")` gives no suggestions; a typed struct would
- **Boilerplate** — every config access requires `.get()` + manual type checking
- **No nested typing** — sub-tables return `Any`, requiring manual recursive extraction
- **Fragile refactoring** — renaming a config key requires searching all string literals

The `TomlTable` wrapper class improves readability but still lacks compile-time type safety:

```saffron
var config = TOML.load("pantry.toml")
var name: String = config.table("package").string("name")  // better, but still stringly-typed
```

With reified types, the same operation becomes:

```saffron
class PackageConfig {
    var name: String
    var version: String
    var authors: List<String>
}

class Manifest {
    var package: PackageConfig
    var dependencies: Map<String, String>
}

var manifest: Manifest = TOML.deserialize(Manifest, "pantry.toml")
manifest.package.name    // String — fully typed, IDE-completable, compile-time checked
```

This pattern is foundational for:
- Package manager manifests (pantry.toml)
- Application configuration files
- API response parsing (JSON from HTTP)
- Data interchange formats (CSV, YAML, MessagePack)
- Generic programming and container libraries

## 2. What "Reified Types" Means for Saffron

In many languages, generic type parameters are erased at runtime (Java generics, for example). Saffron already has partial type reification — classes are first-class values, `42 is Number` works, and `Reflect.type_of(x)` returns a string. But the current system lacks:

1. **Structured field metadata** — knowing that class `Manifest` has field `package` of type `PackageConfig`
2. **Generic type parameters at runtime** — knowing that `List<String>` means "list where elements are strings"
3. **Recursive type navigation** — given a type descriptor for `Manifest`, traversing into `PackageConfig`'s descriptor
4. **Construction from metadata** — building an instance field-by-field using type info to coerce values

"Reified types" in Saffron means: **every class carries a type descriptor that enumerates its fields, their names, and their full type signatures (including generics), accessible both at compile time and at runtime.**

## 3. Required Runtime Metadata

For each class, the following metadata must be available:

### Field Descriptors

| Property | Type | Example |
|----------|------|---------|
| `name` | String | `"authors"` |
| `type_name` | String | `"List<String>"` |
| `type_tag` | Enum | `TypeTag.List` |
| `element_type` | String (nullable) | `"String"` |
| `key_type` | String (nullable) | for Map: `"String"` |
| `value_type` | String (nullable) | for Map: `"String"` |
| `is_optional` | Bool | from `@optional` decorator |
| `default_value` | Any (nullable) | from `@default(...)` |
| `rename_key` | String (nullable) | from `@rename(...)` |

### Type Tag Enumeration

```saffron
enum TypeTag {
    Primitive,       // String, Number, Bool, Nil
    Class,           // user-defined class — recurse
    List,            // List<T> — check element type
    Map,             // Map<K, V> — check key/value types
    Enum,            // enum variant
    Option,          // Option<T> — handle missing as None
    Any              // no type constraint
}
```

### Class Descriptor

```saffron
class TypeDescriptor {
    var class_name: String
    var fields: List<FieldDescriptor>
    var constructor_arity: Number
}

class FieldDescriptor {
    var name: String
    var type_name: String
    var type_tag: TypeTag
    var nested_type: TypeDescriptor   // for Class fields — recursive
    var element_type: String          // for List<T>
    var key_type: String              // for Map<K, V>
    var value_type: String            // for Map<K, V>
    var is_optional: Bool
    var default_value: Any
    var rename_key: String
}
```

## 4. How It Works Today (Reflect Module)

### CVM (Bytecode VM)

The CVM already stores per-class field metadata in `FieldMetaArray`:

```c
typedef struct {
    ObjString *name;       // field name: "version"
    FieldTypeTag typeTag;  // FIELD_TYPE_STRING, FIELD_TYPE_CLASS, etc.
    ObjString *typeName;   // full type string: "String", "PackageConfig"
} FieldMeta;
```

The `Reflect` C module provides:

| Function | What it does |
|----------|-------------|
| `Reflect.fields(instance)` | Returns `Map<String, Any>` of field name to current value |
| `Reflect.field_types(klass)` | Returns `Map<String, String>` of field name to type name |
| `Reflect.construct(klass, map)` | Creates instance, copies map entries into fields |
| `Reflect.construct(klass, map, true)` | Deep construction — recurses into nested class fields |
| `Reflect.class_name(val)` | Returns the class name as a string |
| `Reflect.type_of(val)` | Returns type category string ("number", "instance", etc.) |
| `Reflect.is_instance(val)` | True if value is a class instance |

The `deepConstruct` function in `reflect.c` already handles nested class fields:
- Walks `FieldMetaArray` looking for `FIELD_TYPE_CLASS` entries
- Looks up the nested class by name (from builtins or module frames)
- Recursively constructs nested instances from sub-maps

### What's Missing

1. **Generic type parameters** — `field_types` returns `"List"` not `"List<String>"`, so a generic deserializer cannot validate element types
2. **No List/Map element coercion** — `deepConstruct` handles nested classes but not `List<PackageConfig>` (array of objects)
3. **No default values** — missing keys always produce `nil` fields
4. **No error messages** — type mismatches silently produce wrong values
5. **No decorator metadata** — `@rename`, `@optional`, `@skip` have no runtime representation

### LLVM Compiler (Native Path)

The LLVM compiler tracks field info in `class_fields` as a comma-separated string (`"name:String,version:String,authors:List<String>"`). At codegen time, classes become LLVM struct types:

```llvm
%PackageConfig = type { i64, i64, i64 }   ; name, version, authors
```

Currently there is **no runtime type descriptor emitted** — all type info is consumed at compile time and discarded. The native path has no `Reflect.field_types()` equivalent. This is the primary gap to fill.

## 5. Proposed API

### Basic Deserialization

```saffron
import "@toml" as TOML
import "@json" as JSON

// From file path
var manifest: Manifest = TOML.deserialize(Manifest, "pantry.toml")

// From string
var user: User = JSON.deserialize(User, "{\"name\": \"alice\", \"age\": 30}")

// Generic function signature
// fun deserialize(klass: Class<T>, source: String): T
```

### Options Map

```saffron
var config = TOML.deserialize(Config, "app.toml", {
    "strict": false,                    // ignore unknown keys (default: true)
    "rename": {"pkg": "package"},       // remap source keys to field names
    "coerce": true                      // attempt type coercion (Number -> String, etc.)
})
```

### Partial Deserialization

```saffron
// Only populate specified fields, leave others as nil/default
var partial = TOML.deserialize_partial(Config, "app.toml", ["name", "version"])
```

### Validation

```saffron
// Check if data matches the type without constructing
var errors: List<String> = TOML.validate(Manifest, "pantry.toml")
if (errors.length() > 0) {
    for (e in errors) { IO.println("  - ${e}") }
}
```

### Serialization

```saffron
var toml_str: String = TOML.serialize(manifest)
var json_str: String = JSON.serialize(user)
var pretty: String = JSON.pretty_serialize(user)
```

## 6. Type Metadata Emission (Codegen)

Three approaches for making type info available at runtime in the LLVM-compiled path:

### Option A: Global Type Descriptor Tables

Each class emits a global constant containing field metadata:

```llvm
; String constants for field names and types
@.str.name = private constant [5 x i8] c"name\00"
@.str.String = private constant [7 x i8] c"String\00"
@.str.version = private constant [8 x i8] c"version\00"
@.str.authors = private constant [8 x i8] c"authors\00"
@.str.List_String = private constant [13 x i8] c"List<String>\00"

; Field descriptor array: { name_ptr, type_ptr, type_tag, flags }
@__fields_PackageConfig = private constant [3 x { i8*, i8*, i32, i32 }] [
    { i8* @.str.name, i8* @.str.String, i32 0, i32 0 },
    { i8* @.str.version, i8* @.str.String, i32 0, i32 0 },
    { i8* @.str.authors, i8* @.str.List_String, i32 5, i32 0 }
]

; Type descriptor: { class_name, field_count, fields_ptr }
@__type_desc_PackageConfig = global { i8*, i32, { i8*, i8*, i32, i32 }* } {
    i8* @.str.PackageConfig,
    i32 3,
    { i8*, i8*, i32, i32 }* @__fields_PackageConfig
}
```

A runtime intrinsic `__get_type_descriptor(class_ptr)` returns the descriptor pointer.

### Option B: Constructor Introspection

Instead of emitting new metadata, use the existing `init` function's parameter list. The compiler already knows the parameter names and types. A new intrinsic `Reflect.init_params(klass)` returns them:

```saffron
Reflect.init_params(PackageConfig)
// [{"name": "name", "type": "String"}, {"name": "version", "type": "String"}, ...]
```

Limitation: only works for classes where `init` parameters correspond 1:1 with fields. Does not work for classes with computed/derived fields.

### Option C: Compile-Time Codegen (Recommended Hybrid)

The compiler detects `TOML.deserialize(Manifest, ...)` calls and generates a specialized `__deserialize_Manifest` function at compile time:

```saffron
// Compiler generates this implicitly:
fun __deserialize_Manifest(data: Map<String, Any>): Manifest {
    var pkg_data = data.get("package")
    var pkg = __deserialize_PackageConfig(pkg_data)
    var deps = data.get("dependencies")  // Map<String, String> passes through
    return Manifest(pkg, deps)
}
```

This eliminates runtime reflection entirely — all type checking happens at compile time. The generated code includes proper error messages:

```saffron
fun __deserialize_PackageConfig(data: Any): PackageConfig {
    if (!(data is Map)) {
        throw "deserialize: expected table for PackageConfig, got ${Reflect.type_of(data)}"
    }
    var name = data.get("name")
    if (!(name is String)) {
        throw "deserialize: field 'name' on PackageConfig expects String, got ${Reflect.type_of(name)}"
    }
    // ...
}
```

### Recommendation: Hybrid (Option A + C)

- **Phase 1**: Emit type descriptor tables (Option A) — minimal codegen changes, enables runtime reflection
- **Phase 2**: Add compile-time specialization (Option C) for known-type deserialize calls — zero overhead for the common case
- **Fallback**: When the type is not statically known (e.g., passed as a generic parameter), use the runtime descriptor tables

## 7. Nested Types and Generics

### Nested Class Fields

```saffron
class Manifest {
    var package: PackageConfig    // table → recursively deserialize
}
```

When the deserializer encounters a field typed as a class, it:
1. Extracts the sub-map from the source data
2. Looks up the type descriptor for `PackageConfig`
3. Recursively deserializes the sub-map into a `PackageConfig` instance

### List of Primitives

```saffron
class Config {
    var tags: List<String>
}
```

The type string `"List<String>"` tells the deserializer:
1. Source value must be an array
2. Each element must be (or be coercible to) `String`
3. Construct a `List<String>` from the validated elements

### List of Objects

```saffron
class Project {
    var contributors: List<Person>
}
```

For `List<Person>`:
1. Source must be an array (TOML: array of tables `[[contributors]]`)
2. Each element is a table — recursively deserialize as `Person`
3. Collect into `List<Person>`

### Map Types

```saffron
class Manifest {
    var dependencies: Map<String, String>
}
```

For `Map<String, String>`:
1. Source must be a table/object
2. All keys must be strings (always true in TOML/JSON)
3. All values must be strings — validate and pass through

### Option Types (Missing Keys)

```saffron
class Config {
    var description: Option<String>
}
```

For `Option<String>`:
1. If key is present in source: wrap value as `Option.Some(value)`
2. If key is missing: produce `Option.None`
3. No error thrown for missing keys

### Deeply Nested Generics

```saffron
class Registry {
    var packages: Map<String, List<Version>>
}
```

The deserializer must parse the type string recursively:
- `Map<String, List<Version>>` → table where each value is an array of `Version` objects

Type string parsing uses the same `split_respecting_generics` utility the compiler already has.

## 8. Error Handling

Deserialization errors must be clear, specific, and actionable:

```saffron
// Missing required field
TOML.deserialize(Manifest, "incomplete.toml")
// throws: "TOML deserialize: missing required field 'name' on PackageConfig at path 'package.name'"

// Type mismatch
TOML.deserialize(Config, "bad.toml")
// throws: "TOML deserialize: field 'port' on Config expects Number, got String (value: \"8080\") at path 'port'"

// Unknown key in strict mode
TOML.deserialize(Config, "extra.toml")
// throws: "TOML deserialize: unknown key 'debug_mode' on Config (strict mode). Known fields: name, version, port"

// Nested error with path
TOML.deserialize(Manifest, "nested_bad.toml")
// throws: "TOML deserialize: field 'version' on PackageConfig expects String, got Number at path 'package.version'"

// Array element type mismatch
TOML.deserialize(Config, "bad_array.toml")
// throws: "TOML deserialize: element 2 of field 'tags' on Config expects String, got Number at path 'tags[2]'"
```

### Error Accumulation Mode

```saffron
var result = TOML.try_deserialize(Manifest, "bad.toml")
match (result) {
    Ok(manifest) => use(manifest),
    Err(errors) => {
        for (e in errors) { IO.println("  ${e}") }
    }
}
```

## 9. Decorator-Driven Customization

Decorators on class fields modify deserialization behavior. The parser already supports general `@name` and `@name("arg")` decorators (stored as annotations on the subsequent declaration). This section extends that to per-field decorators.

### @rename — Key Remapping

```saffron
class Config {
    @rename("package-name")
    var package_name: String
    
    @rename("build-dir")
    var build_dir: String
}
```

The TOML key `package-name` maps to the Saffron field `package_name`. This handles the common case where TOML uses kebab-case but Saffron uses snake_case.

### @default — Default Values

```saffron
class Config {
    @default("0.1.0")
    var version: String
    
    @default(8080)
    var port: Number
    
    @default([])
    var tags: List<String>
}
```

If the key is missing from the source, use the default value instead of throwing.

### @optional — Nullable Fields

```saffron
class Config {
    @optional
    var description: String    // nil if missing, no error

    @optional
    var homepage: String
}
```

Similar to `@default(nil)` but semantically distinct — marks the field as intentionally nullable.

### @skip — Exclude from Deserialization

```saffron
class AppState {
    var name: String
    var config_path: String
    
    @skip
    var _cache: Map<String, Any>    // not deserialized, not serialized
    
    @skip
    var _connections: List<Socket>
}
```

Skipped fields are initialized to `nil` (or their default) and ignored during both serialization and deserialization.

### @flatten — Inline Nested Fields

```saffron
class FullConfig {
    @flatten
    var base: BaseConfig       // base's fields are read from the top level, not a sub-table
    
    var extra: String
}
```

### Parser Changes Required

Currently decorators attach to function or class declarations. Field-level decorators require:

1. Extending the parser to recognize `@name` before `var` declarations inside class bodies
2. Storing field annotations in the AST `VarDecl` node (or a new `FieldDecl` node)
3. Propagating annotations to the codegen's `class_fields` metadata

## 10. Implementation Approaches (Comparison)

### Approach 1: Pure Runtime Reflection

**How it works**: Emit type descriptor tables at compile time. At runtime, a generic `deserialize_from_map(klass, data)` function walks the descriptor, extracts fields, validates types, and constructs the instance.

**Pros**:
- Single implementation of deserialization logic (in `@reflect` or `@serde` stdlib module)
- Works with dynamically-determined types (`deserialize(get_class_by_name("Config"), data)`)
- Minimal codegen changes — just emit the descriptor globals

**Cons**:
- Runtime overhead: descriptor lookup, string comparisons, dynamic dispatch
- Error messages generated at runtime (larger binary due to embedded strings)
- Cannot optimize away type checks the compiler already knows will pass

### Approach 2: Pure Compile-Time Codegen

**How it works**: The compiler recognizes `TOML.deserialize(Manifest, ...)` and generates a specialized deserialization function per class at compile time. No runtime metadata needed.

**Pros**:
- Zero runtime overhead — just field accesses and type checks the compiler can optimize
- Errors can be caught at compile time (e.g., trying to deserialize into a class with no String fields from TOML)
- Smallest binary size — no metadata tables

**Cons**:
- Only works when the target type is statically known
- Significant compiler complexity — must generate code for every deserialize call site
- Cannot support patterns like `fun load_any(klass: Any, path: String)` where klass is a parameter
- Code bloat if many classes are deserialized

### Approach 3: Hybrid (Recommended)

**How it works**:
- Emit lightweight type descriptor tables for all classes (field names + type strings)
- Provide a generic runtime deserializer that uses these tables
- Optionally, the compiler can specialize hot paths where the type is statically known

**Pros**:
- Best of both worlds: good performance for static cases, flexibility for dynamic cases
- Incremental implementation — runtime path works first, optimization follows
- Type descriptors serve multiple purposes (serialization, debugging, documentation)

**Cons**:
- Most implementation work overall
- Slightly larger binaries (descriptor tables + maybe specialized functions)

### Decision: Hybrid (Approach 3)

Phase 1 delivers runtime descriptors. Phase 2 adds compile-time specialization as an optimization. The runtime path guarantees correctness; the compile-time path guarantees performance.

## 11. Serialization (Reverse Direction)

### Basic Serialization

```saffron
import "@toml" as TOML
import "@json" as JSON

var manifest = Manifest(
    PackageConfig("saffron", "0.1.0", "A typed language", "main.sf", ["alice"]),
    {"dep-a": "1.0.0", "dep-b": "2.0.0"}
)

// Serialize to format strings
var toml_out: String = TOML.serialize(manifest)
var json_out: String = JSON.serialize(manifest)
var json_pretty: String = JSON.pretty_serialize(manifest)
```

### Existing Support

The `JSON.to_string()` function already handles instance serialization via `Reflect.fields()`:

```saffron
if (Reflect.is_instance(value)) {
    var fields = Reflect.fields(value)
    // ... serialize each field
}
```

This works for the CVM today. The LLVM path needs equivalent support.

### Serialization Respects Decorators

```saffron
class Config {
    @rename("package-name")
    var package_name: String    // serializes key as "package-name"
    
    @skip
    var _cache: Map<String, Any>   // omitted from output
    
    @optional
    var description: String     // omitted if nil
}
```

### TOML Serialization Specifics

TOML has structural rules (tables, arrays of tables) that require awareness of the type:

```saffron
class Manifest {
    var package: PackageConfig       // becomes [package] table
    var dependencies: Map<String, String>  // becomes [dependencies] table
    var targets: List<Target>        // becomes [[targets]] array of tables
}
```

The serializer must:
1. Write primitive fields as top-level key-value pairs
2. Write class-typed fields as `[section]` tables
3. Write `List<Class>` fields as `[[section]]` arrays of tables
4. Write `Map` fields as `[section]` tables

## 12. Integration with Existing Modules

### @toml

```saffron
import "@toml" as TOML

// Existing API (unchanged)
TOML.parse(source)              // Map<String, Any>
TOML.parse_file(path)           // Map<String, Any>
TOML.load(path)                 // TomlTable wrapper
TOML.stringify(map)             // String

// New typed API
TOML.deserialize(Type, path_or_string)      // T
TOML.deserialize_partial(Type, source, fields)  // T (partial)
TOML.serialize(instance)                     // String
TOML.validate(Type, source)                  // List<String>
TOML.try_deserialize(Type, source)           // Result<T, List<String>>
```

### @json

```saffron
import "@json" as JSON

// Existing API (unchanged)
JSON.parse(source)              // Any (Map/List/primitive)
JSON.to_string(value)           // String (compact)
JSON.pretty(value)              // String (indented)
JSON.parse_into(klass, source)  // instance (shallow, untyped)

// New typed API
JSON.deserialize(Type, source)              // T
JSON.serialize(instance)                     // String (compact)
JSON.pretty_serialize(instance)              // String (indented)
JSON.validate(Type, source)                  // List<String>
JSON.try_deserialize(Type, source)           // Result<T, List<String>>
```

### @yaml (future)

```saffron
import "@yaml" as YAML

YAML.deserialize(Type, source)
YAML.serialize(instance)
```

### @serde (new generic module)

A format-agnostic serialization/deserialization module that the format-specific modules delegate to:

```saffron
import "@serde" as Serde

// Core generic functions
Serde.from_map(Type, map)                // Map<String, Any> -> T
Serde.to_map(instance)                   // T -> Map<String, Any>
Serde.validate_map(Type, map)            // List<String>

// Type introspection
Serde.describe(Type)                     // TypeDescriptor
Serde.field_names(Type)                  // List<String>
Serde.field_type(Type, field_name)       // String
```

The TOML/JSON modules parse to `Map<String, Any>` first (which they already do), then delegate to `Serde.from_map()` for typed construction. This avoids duplicating the recursive type-walking logic.

## 13. Implementation Plan

### Phase 1: Type Descriptor Emission (Codegen)

**Goal**: Every class emits a type descriptor table accessible at runtime.

**Changes**:
- `src/compiler/codegen/stmts_body.sf` — in `gen_class_decl`, emit `@__type_desc_ClassName` global
- New runtime function `__sf_get_type_desc(class_ptr) -> descriptor_ptr`
- Descriptor format: array of `{field_name_ptr, type_string_ptr, type_tag_i32, flags_i32}`

**Fields stored**: name, type string (full, including generics), type tag enum, decorator flags

**Estimated effort**: 2-3 days

### Phase 2: Generic `from_map` Deserializer

**Goal**: A stdlib function that takes a type descriptor + Map and produces a typed instance.

**Changes**:
- New file `src/lib/serde.sf` with `from_map(Type, Map<String, Any>): T`
- Walks type descriptor fields, extracts values from map, validates types
- Handles primitives (String, Number, Bool), lists, maps, nested classes
- Produces clear error messages with field paths

**Estimated effort**: 3-4 days

### Phase 3: TOML/JSON `deserialize` Wrappers

**Goal**: User-facing `TOML.deserialize(Type, path)` and `JSON.deserialize(Type, source)`.

**Changes**:
- `src/lib/toml.sf` — add `deserialize(klass, path_or_string)` function
- `src/lib/json.sf` — add `deserialize(klass, source)` function
- Both parse to Map first, then call `Serde.from_map(klass, map)`

**Estimated effort**: 1-2 days

### Phase 4: Field-Level Decorators

**Goal**: Support `@rename`, `@default`, `@optional`, `@skip` on class fields.

**Changes**:
- Parser: recognize `@decorator` before `var` in class bodies
- AST: store annotations on field declarations
- Codegen: include decorator info in type descriptor flags
- Serde: respect decorators during deserialization

**Estimated effort**: 3-4 days

### Phase 5: Serialization

**Goal**: `TOML.serialize(instance)` and `JSON.serialize(instance)` using type descriptors.

**Changes**:
- `src/lib/serde.sf` — add `to_map(instance): Map<String, Any>`
- Walks fields via descriptor, respects `@skip`, applies `@rename` in reverse
- `TOML.serialize` and `JSON.serialize` delegate to `Serde.to_map` then format

**Estimated effort**: 2-3 days

### Phase 6 (Optional): Compile-Time Specialization

**Goal**: Optimize known-type deserialize calls by generating specialized functions.

**Changes**:
- Codegen detects `TOML.deserialize(KnownClass, ...)` patterns
- Emits a direct `__deserialize_KnownClass(map)` function instead of generic path
- Falls back to runtime path for dynamic/generic cases

**Estimated effort**: 5-7 days (complex compiler work)

### Total Estimated Effort

Phases 1-5 (full feature): ~12-16 days
Phase 6 (optimization): additional 5-7 days

## 14. Open Questions

### Should field decorators use the existing `@name` system or a new mechanism?

The parser already handles `@name` and `@name("arg")` on functions and classes. Extending to fields is natural. However, the current system stores annotations as a single string in the docstring field. Multiple decorators on one field (e.g., `@rename("pkg") @default("default")`) require either:
- A list of annotations per field (AST change)
- A combined encoding: `"@rename:pkg,@default:default"` (string concatenation)

**Recommendation**: Store as a list of annotation strings per field. Cleaner and extensible.

### How deep should generic type info go?

For `List<List<Map<String, Number>>>`, the type descriptor string is sufficient for the runtime deserializer to parse and validate. The string format `"List<List<Map<String, Number>>>"` can be parsed with the same `split_respecting_generics` utility used in codegen.

**Depth limit**: No artificial limit. The type string is finite and the recursion depth matches the nesting depth of the source data.

### Should we support enum deserialization (tagged unions)?

Yes, with a convention for how enums are represented in TOML/JSON:

```saffron
enum Status {
    Active,
    Inactive(reason: String)
}

class User {
    var name: String
    var status: Status
}
```

TOML representation options:
```toml
# Option A: string for unit variants, table for data variants
[user]
name = "alice"
status = "Active"

# Or:
[user]
name = "bob"
[user.status]
Inactive = {reason = "vacation"}
```

JSON representation:
```json
{"name": "alice", "status": "Active"}
{"name": "bob", "status": {"Inactive": {"reason": "vacation"}}}
```

**Recommendation**: Support as Phase 4+ feature. Define a convention (string for unit variants, tagged object for data variants) and document it.

### How to handle circular references in serialization?

Saffron's GC handles circular references at the memory level, but serialization cannot represent cycles in TOML/JSON (which are tree-structured).

**Recommendation**:
- Detect cycles during serialization (maintain a visited set)
- Throw a clear error: `"serialize: circular reference detected at path 'a.b.parent'"` 
- Do NOT attempt to serialize circular structures
- Users who need graph serialization can use `@skip` on back-references or implement custom serialization

### Should `deserialize` call `init` or bypass it?

Two approaches:
1. **Bypass init** — directly set fields (current `Reflect.construct` behavior)
2. **Call init** — pass extracted values as constructor arguments

**Recommendation**: Default to bypass-init (field assignment) because:
- `init` may have side effects (network calls, file I/O)
- `init` may validate/transform data in ways that conflict with deserialization
- Field assignment matches what ORMs/serializers in other languages do

Provide `@construct` class decorator to opt into calling `init` instead:

```saffron
@construct
class ValidatedConfig {
    var port: Number
    fun init(port: Number) {
        if (port < 1 or port > 65535) { throw "invalid port" }
        this.port = port
    }
}
```

### How does this interact with the self-hosted compiler bootstrap?

Type descriptors are emitted by the codegen. The `@serde` module is a stdlib `.sf` file that uses the runtime intrinsic `__sf_get_type_desc`. This means:
- Phase 1 requires codegen changes (must bootstrap with gen2 that doesn't have them)
- The codegen changes emit new globals but use no new syntax — safe for gen2
- The `@serde` module is pure Saffron — no bootstrap concern

### What about performance for large configs?

For a typical config file (< 100 fields), runtime reflection is negligible. For large datasets (thousands of records), Phase 6 compile-time specialization eliminates all overhead. The recommended migration path:
1. Start with runtime deserializer (works immediately, fast enough for config)
2. Profile if needed
3. Enable compile-time specialization for hot paths
