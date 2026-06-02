# LLVM IR Generation Library for Saffron

## 1. Motivation

The current Saffron self-hosted compiler emits LLVM IR via raw string concatenation through a `StringBuilder`. For example:

```saffron
this.emit_indent(local + " = add i64 " + lhs + ", " + rhs)
this.emit_indent(gep + " = getelementptr [" + len.to_string() + " x i8], [" + len.to_string() + " x i8]* " + const_name + ", i64 0, i64 0")
```

This approach has several problems:

- **No type safety.** Nothing prevents emitting `add i64 %ptr, %float` -- mismatched types are silent until `llc` or `opt` rejects the output.
- **Brittle string formatting.** Off-by-one in formatting strings, missing commas, or wrong type annotations produce invalid IR that fails late in the pipeline.
- **No structural validation.** Unterminated basic blocks, unreachable code after terminators, and dangling label references are all possible and hard to diagnose.
- **Difficult to extend.** Adding optimization passes, IR analysis, or multi-target support requires parsing the string output back into a structure.
- **Code duplication.** The NaN-boxing tag/untag patterns, GC root push/pop sequences, and closure construction idioms are repeated as string templates throughout the codegen.

A typed library (`@llvm`) would make invalid IR unrepresentable at compile time, reduce code volume in the codegen by 40-60%, and open the door to IR-level optimization passes written in Saffron.

## 2. Core API Design

### Module

The top-level container for an LLVM compilation unit.

```saffron
import "@llvm" as LLVM

var module: LLVM.Module = LLVM.Module("my_program")
module.set_target_triple("arm64-apple-macosx15.0.0")
module.set_data_layout("e-m:o-i64:64-i128:128-n32:64-S128")

// Add globals, functions, type definitions
module.add_global("@counter", LLVM.Type.I64, LLVM.Const.int(0))
module.add_string_constant("hello world")

// Serialize to .ll text
var ir_text: String = module.emit_ir()
```

### Type

An enum representing LLVM types, with factory methods for compound types.

```saffron
enum Type {
    Void,
    I1,
    I8,
    I32,
    I64,
    Ptr,
    Float64,
    Array(element: Type, count: Int),
    Struct(name: String, fields: List<Type>),
    FnType(ret: Type, params: List<Type>, variadic: Bool),
    NamedType(name: String),
    Token
}

// Convenience constructors
fun ptr_type(): Type { return Type.Ptr }
fun i64_type(): Type { return Type.I64 }
fun fn_type(ret: Type, params: List<Type>): Type {
    return Type.FnType(ret, params, false)
}
fun array_type(elem: Type, n: Int): Type {
    return Type.Array(elem, n)
}
fun struct_type(name: String, fields: List<Type>): Type {
    return Type.Struct(name, fields)
}
```

### Value

All SSA values: instruction results, function arguments, constants, global references.

```saffron
enum Value {
    Instruction(name: String, ty: Type),
    Argument(name: String, ty: Type, index: Int),
    Constant(val: Const),
    Global(name: String, ty: Type),
    Undef(ty: Type)
}
```

### Const

Constant values that can appear in instructions or as global initializers.

```saffron
enum Const {
    Int(value: Int, bits: Int),
    Float(value: String),
    Null(ty: Type),
    StringLit(value: String, index: Int),
    StructLit(fields: List<Const>),
    ArrayLit(elements: List<Const>),
    ZeroInit(ty: Type)
}

// Convenience
fun int_const(v: Int): Const { return Const.Int(v, 64) }
fun i32_const(v: Int): Const { return Const.Int(v, 32) }
fun i8_const(v: Int): Const { return Const.Int(v, 8) }
fun null_ptr(): Const { return Const.Null(Type.Ptr) }
fun bool_const(v: Bool): Const { return Const.Int(if (v) 1 else 0, 1) }
```

### Function and FunctionBuilder

```saffron
class FunctionDef {
    var name: String
    var ret_type: Type
    var params: List<Param>
    var blocks: List<BasicBlock>
    var linkage: Linkage
    var attributes: List<String>
    var is_declaration: Bool
}

enum Linkage {
    Default,
    LinkOnceODR,
    External,
    Private,
    Weak
}

class Param {
    var name: String
    var ty: Type
}

class FunctionBuilder {
    var module: Module
    var func: FunctionDef
    var current_block: BlockBuilder
    var local_counter: Int

    fun init(module: Module, name: String, ret: Type, params: List<Param>) {
        // ...
    }

    fun add_attribute(attr: String): FunctionBuilder {
        this.func.attributes.push(attr)
        return this
    }

    fun set_linkage(l: Linkage): FunctionBuilder {
        this.func.linkage = l
        return this
    }

    fun entry_block(): BlockBuilder {
        return this.current_block
    }

    fun new_block(name: String): BlockBuilder {
        var block: BasicBlock = BasicBlock(name)
        this.func.blocks.push(block)
        return BlockBuilder(this, block)
    }

    fun fresh_local(): Value {
        this.local_counter = this.local_counter + 1
        var name: String = "%t" + this.local_counter.to_string()
        return Value.Instruction(name, Type.I64)
    }

    fun build(): FunctionDef {
        return this.func
    }
}
```

### BasicBlock and BlockBuilder

The `BlockBuilder` is the primary instruction-emitting interface.

```saffron
class BasicBlock {
    var label: String
    var instructions: List<Instruction>
    var terminator: Terminator
    var is_terminated: Bool
}

class BlockBuilder {
    var func_builder: FunctionBuilder
    var block: BasicBlock

    // Arithmetic
    fun add(lhs: Value, rhs: Value): Value { ... }
    fun sub(lhs: Value, rhs: Value): Value { ... }
    fun mul(lhs: Value, rhs: Value): Value { ... }
    fun sdiv(lhs: Value, rhs: Value): Value { ... }
    fun srem(lhs: Value, rhs: Value): Value { ... }
    fun and_op(lhs: Value, rhs: Value): Value { ... }
    fun or_op(lhs: Value, rhs: Value): Value { ... }
    fun xor_op(lhs: Value, rhs: Value): Value { ... }
    fun shl(lhs: Value, rhs: Value): Value { ... }
    fun ashr(lhs: Value, rhs: Value): Value { ... }

    // Comparisons (return i1)
    fun icmp_eq(lhs: Value, rhs: Value): Value { ... }
    fun icmp_ne(lhs: Value, rhs: Value): Value { ... }
    fun icmp_slt(lhs: Value, rhs: Value): Value { ... }
    fun icmp_sle(lhs: Value, rhs: Value): Value { ... }
    fun icmp_sgt(lhs: Value, rhs: Value): Value { ... }
    fun icmp_sge(lhs: Value, rhs: Value): Value { ... }

    // Memory
    fun alloca(ty: Type, name: String): Value { ... }
    fun load(ty: Type, ptr: Value): Value { ... }
    fun store(val: Value, ptr: Value) { ... }
    fun gep(base_ty: Type, ptr: Value, indices: List<Value>): Value { ... }
    fun gep_struct(struct_ty: Type, ptr: Value, field_idx: Int): Value { ... }

    // Conversions
    fun ptrtoint(val: Value, to_ty: Type): Value { ... }
    fun inttoptr(val: Value, to_ty: Type): Value { ... }
    fun bitcast(val: Value, to_ty: Type): Value { ... }
    fun zext(val: Value, to_ty: Type): Value { ... }
    fun trunc(val: Value, to_ty: Type): Value { ... }

    // Calls
    fun call(fn_name: String, ret_ty: Type, args: List<Value>): Value { ... }
    fun call_void(fn_name: String, args: List<Value>) { ... }
    fun call_indirect(fn_ptr: Value, fn_ty: Type, args: List<Value>): Value { ... }

    // Control flow (terminators -- each block must have exactly one)
    fun br(target: BasicBlock) { ... }
    fun cond_br(cond: Value, then_bb: BasicBlock, else_bb: BasicBlock) { ... }
    fun switch_br(val: Value, default: BasicBlock, cases: List<SwitchCase>) { ... }
    fun ret(val: Value) { ... }
    fun ret_void() { ... }
    fun unreachable() { ... }

    // Phi
    fun phi(ty: Type, incoming: List<PhiIncoming>): Value { ... }

    // Select
    fun select(cond: Value, then_val: Value, else_val: Value): Value { ... }

    // Utility
    fun is_terminated(): Bool { return this.block.is_terminated }
}

class SwitchCase {
    var value: Const
    var target: BasicBlock
}

class PhiIncoming {
    var value: Value
    var block: BasicBlock
}
```

## 3. Builder Pattern

### Before: Current string emission for a function call

```saffron
// Current codegen (from expr_body.sf)
var arg_strs: List<String> = []
var i: Float = 0
while (i < args.length()) {
    var val: String = this.gen_arg_value(args[i])
    arg_strs.push("i64 " + val)
    i = i + 1
}
var local: String = this.fresh_local()
this.emit_indent(local + " = call i64 @" + resolved_callee + "(" + arg_strs.join(", ") + ")")
```

### After: Library API for the same call

```saffron
var arg_values: List<LLVM.Value> = []
var i: Int = 0
while (i < args.length()) {
    arg_values.push(this.gen_arg_value(args[i]))
    i = i + 1
}
var result: LLVM.Value = bb.call(resolved_callee, LLVM.Type.I64, arg_values)
```

### Before: Closure construction (from closures_body.sf)

```saffron
var closure_raw: String = this.fresh_local()
this.emit_indent(closure_raw + " = call i8* @malloc(i64 16)")
var closure_ptr: String = this.fresh_local()
this.emit_indent(closure_ptr + " = bitcast i8* " + closure_raw + " to [2 x i64]*")
var fn_as_int: String = this.fn_ptr_to_val(fn_type_str, "@" + lambda_name)
var fn_slot: String = this.fresh_local()
this.emit_indent(fn_slot + " = getelementptr [2 x i64], [2 x i64]* " + closure_ptr + ", i64 0, i64 0")
this.emit_indent("store i64 " + fn_as_int + ", i64* " + fn_slot)
var env_slot: String = this.fresh_local()
this.emit_indent(env_slot + " = getelementptr [2 x i64], [2 x i64]* " + closure_ptr + ", i64 0, i64 1")
this.emit_indent("store i64 " + env_as_int + ", i64* " + env_slot)
var result: String = this.typed_ptr_to_val(closure_ptr, "[2 x i64]*")
```

### After: Closure construction with the library

```saffron
var closure: LLVM.Value = LLVM.Closure.build(bb, lambda_name, fn_type, env_value)
// Or using the raw API:
var pair_ty: LLVM.Type = LLVM.array_type(LLVM.Type.I64, 2)
var raw: LLVM.Value = bb.call("malloc", LLVM.Type.Ptr, [LLVM.Const.int(16)])
var closure_ptr: LLVM.Value = bb.bitcast(raw, pair_ty)
var fn_int: LLVM.Value = bb.ptrtoint(LLVM.Value.Global("@" + lambda_name, fn_type), LLVM.Type.I64)
var fn_slot: LLVM.Value = bb.gep(pair_ty, closure_ptr, [LLVM.Const.int(0), LLVM.Const.int(0)])
bb.store(fn_int, fn_slot)
var env_slot: LLVM.Value = bb.gep(pair_ty, closure_ptr, [LLVM.Const.int(0), LLVM.Const.int(1)])
bb.store(env_value, env_slot)
var result: LLVM.Value = bb.ptrtoint(closure_ptr, LLVM.Type.I64)
```

### Before: Match statement tag extraction (from match_body.sf)

```saffron
var tag_val: String = this.fresh_local()
if (max_fields <= 1) {
    var tag_shifted: String = this.fresh_local()
    this.emit_indent(tag_shifted + " = lshr i64 " + subj + ", 56")
    this.emit_indent(tag_val + " = trunc i64 " + tag_shifted + " to i8")
} else {
    var arr_ptr: String = this.val_to_typed_ptr(subj, "i64*")
    var tag_i64: String = this.fresh_local()
    this.emit_indent(tag_i64 + " = load i64, i64* " + arr_ptr)
    this.emit_indent(tag_val + " = trunc i64 " + tag_i64 + " to i8")
}
```

### After: Tag extraction with the library

```saffron
var tag_val: LLVM.Value = if (max_fields <= 1) {
    var shifted: LLVM.Value = bb.lshr(subj, LLVM.Const.int(56))
    bb.trunc(shifted, LLVM.Type.I8)
} else {
    var arr_ptr: LLVM.Value = bb.inttoptr(subj, LLVM.Type.Ptr)
    var tag_i64: LLVM.Value = bb.load(LLVM.Type.I64, arr_ptr)
    bb.trunc(tag_i64, LLVM.Type.I8)
}
```

## 4. Type Safety Guarantees

The library eliminates entire categories of bugs that plague string-based emission:

| Bug class | Current risk | With library |
|-----------|-------------|--------------|
| Type mismatch in binary ops | `add i64 %ptr_val, %float_val` compiles silently | `bb.add()` only accepts values of matching integer types |
| Unterminated basic block | Missing `br`/`ret` at block end | `Module.emit_ir()` checks every block has a terminator |
| Double termination | `emit_indent` after block is terminated (guarded by `block_terminated` flag) | `BlockBuilder` methods no-op or error after terminator |
| Malformed GEP indices | Wrong array size in getelementptr string | `bb.gep()` validates index types against the base type |
| Dangling label references | `br label %foo` where `foo` is never emitted | `BasicBlock` references are object handles, not strings |
| Wrong calling convention | Missing env parameter in indirect call | Closure helper enforces env-first convention |
| Forgotten return type | `call i64 @fn()` when fn returns `ptr` | Return type carried on `FunctionDef`, validated at call site |
| Invalid SSA (use before def) | Referencing `%t5` before its defining instruction | Values are opaque handles; the emitter orders them correctly |

## 5. IR Serialization

The `Module` class provides methods to serialize the constructed IR to LLVM's textual format:

```saffron
class Module {
    // ...

    fun emit_ir(): String {
        var sb: StringBuilder = StringBuilder()
        this.emit_header(sb)
        this.emit_type_definitions(sb)
        this.emit_string_constants(sb)
        this.emit_global_declarations(sb)
        this.emit_external_declarations(sb)
        this.emit_functions(sb)
        return sb.to_string()
    }

    fun emit_header(sb: StringBuilder) {
        sb.append("; Generated by Saffron LLVM Library\n")
        if (this.target_triple.length() > 0) {
            sb.append("target triple = \"" + this.target_triple + "\"\n")
        }
        if (this.data_layout.length() > 0) {
            sb.append("target datalayout = \"" + this.data_layout + "\"\n")
        }
        sb.append("\n")
    }
}
```

Each `Instruction` variant knows how to format itself:

```saffron
fun format_instruction(inst: Instruction): String {
    return match (inst) {
        Add(result, lhs, rhs) =>
            result.name + " = add " + format_type(lhs.ty) + " " + format_value(lhs) + ", " + format_value(rhs)
        Load(result, ty, ptr) =>
            result.name + " = load " + format_type(ty) + ", " + format_type(ty) + "* " + format_value(ptr)
        Store(val, ptr) =>
            "store " + format_type(val.ty) + " " + format_value(val) + ", " + format_type(val.ty) + "* " + format_value(ptr)
        Call(result, fn_name, ret_ty, args) => {
            var args_str: String = args.map(fun (a: Value): String => format_type(a.ty) + " " + format_value(a)).join(", ")
            result.name + " = call " + format_type(ret_ty) + " @" + fn_name + "(" + args_str + ")"
        }
        // ... other variants
    }
}
```

Future extension: bitcode output via an `emit_bitcode()` method that writes the binary LLVM bitcode format directly (avoiding the textual IR parsing step in the LLVM toolchain).

## 6. Saffron Idioms

### Enums for instruction representation

The entire instruction set is modeled as a Saffron enum:

```saffron
enum Instruction {
    // Arithmetic
    Add(result: Value, lhs: Value, rhs: Value),
    Sub(result: Value, lhs: Value, rhs: Value),
    Mul(result: Value, lhs: Value, rhs: Value),
    SDiv(result: Value, lhs: Value, rhs: Value),
    SRem(result: Value, lhs: Value, rhs: Value),

    // Bitwise
    And(result: Value, lhs: Value, rhs: Value),
    Or(result: Value, lhs: Value, rhs: Value),
    Xor(result: Value, lhs: Value, rhs: Value),
    Shl(result: Value, lhs: Value, rhs: Value),
    AShr(result: Value, lhs: Value, rhs: Value),

    // Comparison
    ICmp(result: Value, pred: CmpPred, lhs: Value, rhs: Value),

    // Memory
    Alloca(result: Value, ty: Type, name: String),
    Load(result: Value, ty: Type, ptr: Value),
    Store(val: Value, ptr: Value),
    GEP(result: Value, base_ty: Type, ptr: Value, indices: List<Value>),

    // Conversions
    PtrToInt(result: Value, val: Value, to_ty: Type),
    IntToPtr(result: Value, val: Value, to_ty: Type),
    Bitcast(result: Value, val: Value, to_ty: Type),
    ZExt(result: Value, val: Value, to_ty: Type),
    Trunc(result: Value, val: Value, to_ty: Type),

    // Calls
    Call(result: Value, fn_name: String, ret_ty: Type, args: List<Value>),
    CallVoid(fn_name: String, args: List<Value>),
    CallIndirect(result: Value, fn_ptr: Value, fn_ty: Type, args: List<Value>),

    // Phi
    Phi(result: Value, ty: Type, incoming: List<PhiIncoming>),

    // Select
    Select(result: Value, cond: Value, then_val: Value, else_val: Value)
}

enum Terminator {
    Br(target: BasicBlock),
    CondBr(cond: Value, then_bb: BasicBlock, else_bb: BasicBlock),
    Switch(val: Value, default: BasicBlock, cases: List<SwitchCase>),
    Ret(val: Value),
    RetVoid,
    Unreachable
}

enum CmpPred {
    Eq, Ne, Slt, Sle, Sgt, Sge, Ult, Ule, Ugt, Uge
}
```

### Method chaining on builders

```saffron
var fb: LLVM.FunctionBuilder = module.add_function("my_func", LLVM.Type.I64, params)
    .set_linkage(LLVM.Linkage.LinkOnceODR)
    .add_attribute("noinline")

var entry: LLVM.BlockBuilder = fb.entry_block()
var x: LLVM.Value = entry.alloca(LLVM.Type.I64, "x")
entry.store(LLVM.Const.int(42), x)
var loaded: LLVM.Value = entry.load(LLVM.Type.I64, x)
entry.ret(loaded)
```

### Pattern matching for IR traversal

```saffron
// Example: count allocas in a function (useful for optimization passes)
fun count_allocas(func: LLVM.FunctionDef): Int {
    var count: Int = 0
    for (block in func.blocks) {
        for (inst in block.instructions) {
            match (inst) {
                Alloca(r, t, n) => { count = count + 1 }
                _ => {}
            }
        }
    }
    return count
}
```

### Iterator-based block traversal

```saffron
// Walk all instructions in a function
for (block in func.blocks) {
    for (inst in block.instructions) {
        // process each instruction
    }
}

// Walk predecessors (future: CFG analysis)
for (pred in block.predecessors()) {
    // ...
}
```

## 7. NaN-Boxing Helpers

Saffron uses NaN-boxing: all runtime values are i64. Pointers, floats, bools, and nil are encoded with tag bits. The library provides a `NanBox` module with high-level operations:

```saffron
class NanBox {
    var bb: BlockBuilder
    var target: String

    fun init(bb: BlockBuilder, target: String) {
        this.bb = bb
        this.target = target
    }

    // Tag an integer value (calls runtime __val_tag_int)
    fun tag_int(raw: Value): Value {
        return this.bb.call("__val_tag_int", Type.I64, [raw])
    }

    // Untag an integer value
    fun untag_int(boxed: Value): Value {
        return this.bb.call("__val_untag_int", Type.I64, [boxed])
    }

    // Tag a pointer as a value
    fun tag_ptr(ptr: Value): Value {
        return this.bb.call("__val_tag_ptr", Type.I64, [ptr])
    }

    // Untag a pointer from a value
    fun untag_ptr(boxed: Value): Value {
        return this.bb.call("__val_untag_ptr", Type.Ptr, [boxed])
    }

    // Tag a float (double -> i64 NaN-boxed)
    fun tag_float(f: Value): Value {
        return this.bb.call("__val_tag_float", Type.I64, [f])
    }

    // Untag a float
    fun untag_float(boxed: Value): Value {
        return this.bb.call("__val_untag_float", Type.Float64, [boxed])
    }

    // Tag a boolean
    fun tag_bool(b: Value): Value {
        return this.bb.call("__val_tag_bool", Type.I64, [b])
    }

    // Untag a boolean
    fun untag_bool(boxed: Value): Value {
        return this.bb.call("__val_untag_bool", Type.I64, [boxed])
    }

    // Nil constant
    fun nil(): Value {
        return this.bb.call("__val_nil", Type.I64, [])
    }

    // Platform-aware pointer-to-value conversion
    // On wasm32: ptrtoint to i32, then zext to i64
    // On native: direct ptrtoint to i64
    fun ptr_to_val(ptr: Value): Value {
        if (this.target == "wasm32") {
            var i32_val: Value = this.bb.ptrtoint(ptr, Type.I32)
            return this.bb.zext(i32_val, Type.I64)
        }
        return this.bb.ptrtoint(ptr, Type.I64)
    }

    // Platform-aware value-to-pointer conversion
    fun val_to_ptr(val: Value): Value {
        if (this.target == "wasm32") {
            var i32_val: Value = this.bb.trunc(val, Type.I32)
            return this.bb.inttoptr(i32_val, Type.Ptr)
        }
        return this.bb.inttoptr(val, Type.Ptr)
    }

    // Enum tag operations: extract tag from (tag << 56) | value encoding
    fun extract_simple_tag(val: Value): Value {
        var shifted: Value = this.bb.lshr(val, LLVM.Const.int(56))
        return this.bb.trunc(shifted, Type.I8)
    }

    // Extract payload from simple enum encoding: value & 0x00FFFFFFFFFFFFFF
    fun extract_simple_payload(val: Value): Value {
        return this.bb.and_op(val, LLVM.Const.int(72057594037927935))
    }

    // Build a simple enum value: (tag << 56) | payload
    fun build_simple_enum(tag: Int, payload: Value): Value {
        var tag_shifted: Value = this.bb.shl(LLVM.Const.int(tag), LLVM.Const.int(56))
        return this.bb.or_op(tag_shifted, payload)
    }
}
```

## 8. GC Integration

The library provides a `GCRoots` helper that manages shadow stack push/pop around function bodies:

```saffron
class GCRoots {
    var bb: BlockBuilder
    var root_count: Int
    var identity_mode: Bool

    fun init(bb: BlockBuilder, identity_mode: Bool) {
        this.bb = bb
        this.root_count = 0
        this.identity_mode = identity_mode
    }

    // Register a local variable's alloca as a GC root
    fun push_root(alloca_ptr: Value) {
        if (this.identity_mode) return nil
        var as_int: Value = this.bb.ptrtoint(alloca_ptr, Type.I64)
        this.bb.call_void("__gc_push_root", [as_int])
        this.root_count = this.root_count + 1
    }

    // Pop all roots registered by this function (call before ret)
    fun pop_all() {
        if (this.identity_mode) return nil
        if (this.root_count == 0) return nil
        this.bb.call_void("__gc_pop_roots", [LLVM.Const.int(this.root_count)])
    }

    // Enable the GC (called at program entry)
    fun enable() {
        if (this.identity_mode) return nil
        this.bb.call_void("__gc_enable", [])
    }

    // GC-aware allocation helpers
    fun alloc(size: Value): Value {
        return this.bb.call("__gc_alloc", Type.Ptr, [size])
    }

    fun alloc_zeroed(size: Value): Value {
        return this.bb.call("__gc_alloc_zeroed", Type.Ptr, [size])
    }

    // Zero-initialize an alloca (prevents GC from reading garbage)
    fun zero_init(alloca_ptr: Value) {
        this.bb.store(LLVM.Const.int(0), alloca_ptr)
    }
}
```

Usage pattern in the codegen:

```saffron
fun gen_function_body(fb: LLVM.FunctionBuilder, params: List<Param>, body: List<Stmt>) {
    var entry: LLVM.BlockBuilder = fb.entry_block()
    var gc: LLVM.GCRoots = LLVM.GCRoots(entry, this.identity_mode)

    // Emit allocas for params and locals
    for (p in params) {
        var alloca: LLVM.Value = entry.alloca(LLVM.Type.I64, p.name)
        entry.store(p.value, alloca)
        if (is_heap_type(p.type)) {
            gc.zero_init(alloca)  // prevent GC reads of garbage
            gc.push_root(alloca)
        }
    }

    // ... generate body ...

    // Before every return:
    gc.pop_all()
    entry.ret(result)
}
```

## 9. Coroutine Support

LLVM coroutines require a specific intrinsic sequence. The library encapsulates this pattern:

```saffron
class CoroBuilder {
    var fb: FunctionBuilder
    var entry_bb: BlockBuilder
    var coro_id: Value
    var coro_hdl: Value
    var final_bb: BasicBlock
    var cleanup_bb: BasicBlock
    var suspend_bb: BasicBlock

    fun init(fb: FunctionBuilder) {
        this.fb = fb
        this.entry_bb = fb.entry_block()

        // Emit coroutine preamble
        this.coro_id = this.entry_bb.call_token("llvm.coro.id", [
            LLVM.Const.i32(0), LLVM.Const.null_ptr(),
            LLVM.Const.null_ptr(), LLVM.Const.null_ptr()
        ])
        var coro_size: Value = this.entry_bb.call("llvm.coro.size.i64", Type.I64, [])
        var coro_mem: Value = this.entry_bb.call("malloc", Type.Ptr, [coro_size])
        this.coro_hdl = this.entry_bb.call_ptr("llvm.coro.begin", [this.coro_id, coro_mem])

        // Pre-create the epilogue blocks
        this.final_bb = fb.new_block("__coro_final").block
        this.cleanup_bb = fb.new_block("__coro_cleanup").block
        this.suspend_bb = fb.new_block("__coro_suspend").block
    }

    // Emit a suspend point. Returns a BlockBuilder positioned at the resume point.
    fun suspend(bb: BlockBuilder): BlockBuilder {
        var save_tok: Value = bb.call_token("llvm.coro.save", [this.coro_hdl])
        var susp_result: Value = bb.call("llvm.coro.suspend", Type.I8, [save_tok, LLVM.Const.bool(false)])

        var resume_bb: BasicBlock = this.fb.new_block("yield.resume").block
        var yield_cleanup: BasicBlock = this.fb.new_block("yield.cleanup").block

        bb.switch_br(susp_result, this.suspend_bb, [
            LLVM.SwitchCase(LLVM.Const.i8(0), resume_bb),
            LLVM.SwitchCase(LLVM.Const.i8(1), yield_cleanup)
        ])

        // Yield cleanup branches to shared cleanup
        var cleanup_builder: BlockBuilder = BlockBuilder(this.fb, yield_cleanup)
        cleanup_builder.br(this.cleanup_bb)

        // Return builder at resume point
        return BlockBuilder(this.fb, resume_bb)
    }

    // Emit the coroutine epilogue (final suspend + cleanup + destroy)
    fun finalize(last_bb: BlockBuilder) {
        // Branch from last body block to final suspend
        if (!last_bb.is_terminated()) {
            last_bb.br(this.final_bb)
        }

        // Final suspend (i1 true = final)
        var final_builder: BlockBuilder = BlockBuilder(this.fb, this.final_bb)
        var fs_tok: Value = final_builder.call_token("llvm.coro.save", [this.coro_hdl])
        var fs_result: Value = final_builder.call("llvm.coro.suspend", Type.I8, [fs_tok, LLVM.Const.bool(true)])

        var trap_bb: BasicBlock = this.fb.new_block("__coro_trap").block
        final_builder.switch_br(fs_result, this.suspend_bb, [
            LLVM.SwitchCase(LLVM.Const.i8(0), trap_bb),
            LLVM.SwitchCase(LLVM.Const.i8(1), this.cleanup_bb)
        ])

        // Trap: unreachable (resumed after final suspend = bug)
        var trap_builder: BlockBuilder = BlockBuilder(this.fb, trap_bb)
        trap_builder.unreachable()

        // Cleanup: free coroutine memory
        var cleanup_builder: BlockBuilder = BlockBuilder(this.fb, this.cleanup_bb)
        var coro_free: Value = cleanup_builder.call_ptr("llvm.coro.free", [this.coro_id, this.coro_hdl])
        cleanup_builder.call_void("free", [coro_free])
        cleanup_builder.br(this.suspend_bb)

        // Suspend: end + ret handle
        var suspend_builder: BlockBuilder = BlockBuilder(this.fb, this.suspend_bb)
        suspend_builder.call("llvm.coro.end", Type.I1, [this.coro_hdl, LLVM.Const.bool(false)])
        suspend_builder.ret(this.coro_hdl)
    }
}
```

Usage:

```saffron
// Building a coroutine function
var fb: LLVM.FunctionBuilder = module.add_function("my_coro", Type.Ptr, params)
    .add_attribute("noinline")
    .add_attribute("optnone")
    .add_attribute("presplitcoroutine")

var coro: LLVM.CoroBuilder = LLVM.CoroBuilder(fb)
var bb: LLVM.BlockBuilder = fb.entry_block()

// ... emit body instructions on bb ...

// At a yield point:
bb = coro.suspend(bb)
// bb is now positioned after resume

// Finalize
coro.finalize(bb)
```

## 10. Migration Path

The library can be adopted incrementally without rewriting the entire codegen at once:

### Phase 1: Parallel output (validation)

- Implement the `@llvm` library as a standalone module
- Add a flag to the compiler (`--validate-ir`) that constructs the IR using both the old string approach AND the new library, then diffs the outputs
- Fix discrepancies until both produce identical `.ll` files

### Phase 2: Replace leaf functions first

Start with self-contained codegen methods that have clear inputs/outputs:

1. `gen_enum_construct` -- simple, isolated pattern
2. `gen_binary` -- arithmetic/comparison instructions
3. `gen_list_lit` -- malloc + store sequence
4. `gen_string_concat` -- strlen + malloc + strcpy sequence

Each method can be converted independently because they return a `Value` (currently a string register name, will become an `LLVM.Value`).

### Phase 3: Replace the function framework

Convert `gen_function` and `gen_closure_function` to use `FunctionBuilder`. This requires:

- The `Codegen` class to hold a `LLVM.Module` instead of / alongside `this.sb`
- Local variable tracking to use `LLVM.Value` instead of string register names
- Block management to use `BasicBlock` objects instead of label strings

### Phase 4: Full migration

Convert remaining methods: `gen_match`, `gen_method_call`, `gen_call`, `gen_if`, `gen_while`, `gen_try_catch`. Remove the `StringBuilder sb` field entirely.

### Compatibility constraint

The `@llvm` library itself must be compilable by gen2 (the bootstrap compiler). This means:

- No tuple literals in the library source
- Use only features the current gen2 supports
- The library can use all standard Saffron features: classes, enums, pattern matching, closures, generics, imports

## 11. Implementation Plan

### Phase 1: Core types and serialization (1-2 weeks)

**Deliverable:** `src/lib/llvm.sf` with Type, Value, Const enums; Instruction/Terminator enums; Module, FunctionDef, BasicBlock data classes; and `emit_ir()` serialization.

**Test:** Construct a simple function (add two i64 params, return result) via the API and verify `emit_ir()` produces valid LLVM IR that `llc` accepts.

Files to create:
- `src/lib/llvm/types.sf` -- Type, Value, Const, CmpPred enums
- `src/lib/llvm/instructions.sf` -- Instruction, Terminator enums
- `src/lib/llvm/module.sf` -- Module class with emit_ir()
- `src/lib/llvm/function.sf` -- FunctionDef, FunctionBuilder, Param
- `src/lib/llvm/block.sf` -- BasicBlock, BlockBuilder
- `src/lib/llvm.sf` -- re-exports (the importable module)

### Phase 2: BlockBuilder instruction methods (1 week)

**Deliverable:** All instruction-emitting methods on BlockBuilder covering arithmetic, memory, conversions, calls, and terminators.

**Test:** Port `gen_binary` to use the library API; verify output matches current codegen for all operators.

### Phase 3: High-level helpers (1 week)

**Deliverable:** NanBox, GCRoots, CoroBuilder, Closure helper classes.

**Test:** Port `gen_lambda` (closure construction) and verify closures work end-to-end through bootstrap.

### Phase 4: Codegen integration (2-3 weeks)

**Deliverable:** New codegen backend that uses `@llvm` throughout. The old string-based backend remains as fallback (selectable via flag).

**Test:** Full bootstrap succeeds using the new backend. All test/*.sf files pass.

### Phase 5: Optimization passes (future)

Once the IR is a structured data type, we can implement passes in Saffron:
- Dead code elimination (remove unreachable blocks)
- Constant folding (evaluate constant expressions at compile time)
- Redundant load elimination (load after store to same address)
- Tail call optimization (detect and annotate tail calls)
- Closure inlining (inline small closures at call site)

These passes operate on `List<BasicBlock>` and `List<Instruction>` using pattern matching -- a natural fit for Saffron's language features.

---

## Appendix: LLVM Features Used by Current Codegen

Based on analysis of the current compiler source, these LLVM features must be supported:

**Types:** i1, i8, i32, i64, ptr (opaque), void, token, `[N x i64]`, `[N x i8]`, `%StructName`, function pointer types

**Instructions:** add, sub, mul, sdiv, srem, and, or, xor, shl, ashr, lshr, icmp (eq/ne/slt/sle/sgt/sge), alloca, load, store, getelementptr, ptrtoint, inttoptr, bitcast, zext, trunc, call, br, switch, ret, unreachable, phi (currently avoided via alloca/load pattern, but could be used with optimization)

**Globals:** Named global variables (`@__g_*`), string constants (`@.str.*`), extern declarations

**Function attributes:** noinline, optnone, presplitcoroutine, linkonce_odr linkage

**Intrinsics:** llvm.coro.id, llvm.coro.size.i64, llvm.coro.begin, llvm.coro.save, llvm.coro.suspend, llvm.coro.end, llvm.coro.free, llvm.coro.done, llvm.memcpy.p0i8.p0i8.i64, llvm.floor.f64

**Target-specific:** wasm32 vs native pointer width handling (i32 vs i64 for ptrtoint/inttoptr), target triple and data layout strings
