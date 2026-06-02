target triple = "arm64-apple-macosx14.0.0"

; Globals written by the codegen-emitted main() wrapper
@__argc = weak global i32 0
@__argv = weak global i8** null

; Exception handling state (used by codegen try/catch emission)
@__exception_value = weak global i64 0
@__jmp_buf_stack = weak global [64 x i8] zeroinitializer

; Async scheduler state
@__yield_reason = global i64 0
@__yield_arg = global i64 0
@__task_result = global i64 0
@__coro_escape_sink = global ptr null

declare double @sf_time_now()
declare i64 @sf_select_fds(i64*, i64, i64*, i64, i64)

; --- Typed IO dispatch (compile-time polymorphism) ---
; All values are i64. The codegen picks the right variant at compile time.

declare i32 @puts(i8*)
declare i32 @printf(i8*, ...)

@.fmt.ld_nl = private unnamed_addr constant [5 x i8] c"%ld\0A\00"
@.fmt.ld = private unnamed_addr constant [4 x i8] c"%ld\00"
@.str.true_nl = private unnamed_addr constant [5 x i8] c"true\00"
@.str.false_nl = private unnamed_addr constant [6 x i8] c"false\00"
@.str.nil_nl = private unnamed_addr constant [4 x i8] c"nil\00"
@.str.pct_s = private unnamed_addr constant [3 x i8] c"%s\00"

define void @__io_println_str(i64 %s) {
entry:
  %ptr = inttoptr i64 %s to i8*
  call i32 @puts(i8* %ptr)
  ret void
}

define void @__io_println_int(i64 %n) {
entry:
  %fmt = getelementptr [5 x i8], [5 x i8]* @.fmt.ld_nl, i64 0, i64 0
  call i32 (i8*, ...) @printf(i8* %fmt, i64 %n)
  ret void
}

define void @__io_println_bool(i64 %b) {
entry:
  %is_true = icmp ne i64 %b, 0
  br i1 %is_true, label %yes, label %no
yes:
  %t = getelementptr [5 x i8], [5 x i8]* @.str.true_nl, i64 0, i64 0
  call i32 @puts(i8* %t)
  ret void
no:
  %f = getelementptr [6 x i8], [6 x i8]* @.str.false_nl, i64 0, i64 0
  call i32 @puts(i8* %f)
  ret void
}

define void @__io_println_nil() {
entry:
  %s = getelementptr [4 x i8], [4 x i8]* @.str.nil_nl, i64 0, i64 0
  call i32 @puts(i8* %s)
  ret void
}

define void @__io_print_str(i64 %s) {
entry:
  %ptr = inttoptr i64 %s to i8*
  %fmt = getelementptr [3 x i8], [3 x i8]* @.str.pct_s, i64 0, i64 0
  call i32 (i8*, ...) @printf(i8* %fmt, i8* %ptr)
  ret void
}

define void @__io_print_int(i64 %n) {
entry:
  %fmt = getelementptr [4 x i8], [4 x i8]* @.fmt.ld, i64 0, i64 0
  call i32 (i8*, ...) @printf(i8* %fmt, i64 %n)
  ret void
}

; --- to_string helpers for compile-time polymorphism ---

define i64 @__bool_to_string(i64 %b) {
entry:
  %is_true = icmp ne i64 %b, 0
  br i1 %is_true, label %yes, label %no
yes:
  %t = getelementptr [5 x i8], [5 x i8]* @.str.true_nl, i64 0, i64 0
  %r1 = ptrtoint i8* %t to i64
  ret i64 %r1
no:
  %f = getelementptr [6 x i8], [6 x i8]* @.str.false_nl, i64 0, i64 0
  %r2 = ptrtoint i8* %f to i64
  ret i64 %r2
}

; Wrapper: tag a raw pointer as a Saffron string value (i64 → i64)
define i64 @__rt_tag_ptr(i64 %raw) {
entry:
  %ptr = inttoptr i64 %raw to i8*
  %tagged = call i64 @__val_tag_ptr(i8* %ptr)
  ret i64 %tagged
}
define i64 @__nil_to_string() {
entry:
  %s = getelementptr [4 x i8], [4 x i8]* @.str.nil_nl, i64 0, i64 0
  %r = ptrtoint i8* %s to i64
  ret i64 %r
}

@.fmt.float = private unnamed_addr constant [3 x i8] c"%g\00"

define i64 @__float_to_string(i64 %v) {
entry:
  %f = bitcast i64 %v to double
  %buf = call i8* @malloc(i64 32)
  %fmt = getelementptr [3 x i8], [3 x i8]* @.fmt.float, i64 0, i64 0
  call i32 (i8*, i64, i8*, ...) @snprintf(i8* %buf, i64 32, i8* %fmt, double %f)
  %r = ptrtoint i8* %buf to i64
  ret i64 %r
}

declare i8* @malloc(i64)
declare i32 @snprintf(i8*, i64, i8*, ...)

; =============================================================================
; NaN-Boxing Infrastructure
; =============================================================================
;
; IEEE 754 double: if exponent bits are all 1 and mantissa != 0, it's a NaN.
; We use the quiet NaN space (bit 51 set) to encode non-float values.
;
; Encoding:
;   Valid double (not NaN)         → Float (stored directly, zero cost)
;   0x7FF8_xxxx_xxxx_xxxx         → Heap pointer (48-bit address in payload)
;   0x7FF9_xxxx_xxxx_xxxx         → Integer (48-bit signed in payload)
;   0x7FFA_0000_0000_0001         → true
;   0x7FFA_0000_0000_0000         → false
;   0x7FFA_0000_0000_0002         → nil
;
; Heap object layout: first i64 is the type tag
;   TYPE_STRING = 1, TYPE_LIST = 2, TYPE_MAP = 3, TYPE_CLASS_BASE = 4+

; --- Constants ---
; TAG_PTR  = 0x7FF8_0000_0000_0000
; TAG_INT  = 0x7FF9_0000_0000_0000
; TAG_SPEC = 0x7FFA_0000_0000_0000
; PAYLOAD_MASK = 0x0000_FFFF_FFFF_FFFF

; Heap type IDs
@__TYPE_STRING = constant i64 1
@__TYPE_LIST = constant i64 2
@__TYPE_MAP = constant i64 3
@__TYPE_FLOAT_BOXED = constant i64 4

; --- Tag/Untag Helpers ---
; IDENTITY mode (backward compatible). Real NaN-boxing implementations
; are ready below in comments — swap when runtime is also NaN-box aware.

define i64 @__val_tag_int(i64 %n) {
entry:
  ret i64 %n
}

define i64 @__val_untag_int(i64 %v) {
entry:
  ret i64 %v
}

define i64 @__val_tag_ptr(i8* %ptr) {
entry:
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

define i8* @__val_untag_ptr(i64 %v) {
entry:
  %ptr = inttoptr i64 %v to i8*
  ret i8* %ptr
}

define i64 @__val_tag_float(double %f) {
entry:
  %bits = bitcast double %f to i64
  ret i64 %bits
}

define double @__val_untag_float(i64 %v) {
entry:
  %f = bitcast i64 %v to double
  ret double %f
}

define i64 @__val_tag_bool(i64 %b) {
entry:
  ret i64 %b
}

define i64 @__val_untag_bool(i64 %v) {
entry:
  ret i64 %v
}

define i64 @__val_nil() {
entry:
  ret i64 0
}

; --- Type Checking ---

define i1 @__val_is_float(i64 %v) {
entry:
  ; A value is a float if it's NOT a NaN (or is a real NaN from float arithmetic)
  ; Check: (v & 0x7FF0000000000000) != 0x7FF0000000000000 OR mantissa == 0
  ; Simpler: check that the upper 13 bits are NOT 0x7FF8..0x7FFA (our tag range)
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760         ; 0x7FF8
  %is_int = icmp eq i64 %upper, 32761         ; 0x7FF9
  %is_spec = icmp eq i64 %upper, 32762        ; 0x7FFA
  %not_ptr = xor i1 %is_ptr, true
  %not_int = xor i1 %is_int, true
  %not_spec = xor i1 %is_spec, true
  %a = and i1 %not_ptr, %not_int
  %result = and i1 %a, %not_spec
  ret i1 %result
}

define i1 @__val_is_int(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %result = icmp eq i64 %upper, 32761         ; 0x7FF9
  ret i1 %result
}

define i1 @__val_is_ptr(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %result = icmp eq i64 %upper, 32760         ; 0x7FF8
  ret i1 %result
}

define i1 @__val_is_nil(i64 %v) {
entry:
  ; nil = 0x7FFA000000000002
  %result = icmp eq i64 %v, 9222246136947933186 ; 0x7FFA000000000002
  ret i1 %result
}

define i1 @__val_is_true(i64 %v) {
entry:
  ; true = 0x7FFA000000000001
  %result = icmp eq i64 %v, 9222246136947933185 ; 0x7FFA000000000001
  ret i1 %result
}

define i1 @__val_is_bool(i64 %v) {
entry:
  %is_t = icmp eq i64 %v, 9222246136947933185  ; true
  %is_f = icmp eq i64 %v, 9222246136947933184  ; false (0x7FFA000000000000)
  %result = or i1 %is_t, %is_f
  ret i1 %result
}

; --- Heap Object Type ID ---

define i64 @__val_type_id(i64 %v) {
entry:
  ; For a heap pointer, read the type ID from the first field of the object
  %ptr_int = and i64 %v, 281474976710655      ; mask off tag
  %ptr = inttoptr i64 %ptr_int to i64*
  %type_id = load i64, i64* %ptr
  ret i64 %type_id
}

define i1 @__val_is_string(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760
  br i1 %is_ptr, label %check, label %no
check:
  %tid = call i64 @__val_type_id(i64 %v)
  %result = icmp eq i64 %tid, 1               ; TYPE_STRING
  ret i1 %result
no:
  ret i1 false
}

define i1 @__val_is_list(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760
  br i1 %is_ptr, label %check, label %no
check:
  %tid = call i64 @__val_type_id(i64 %v)
  %result = icmp eq i64 %tid, 2               ; TYPE_LIST
  ret i1 %result
no:
  ret i1 false
}

define i1 @__val_is_map(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760
  br i1 %is_ptr, label %check, label %no
check:
  %tid = call i64 @__val_type_id(i64 %v)
  %result = icmp eq i64 %tid, 3               ; TYPE_MAP
  ret i1 %result
no:
  ret i1 false
}

; --- NaN-Boxing Constants (for codegen to emit directly) ---
; TAG_INT_CONST  = 9221120237041090560  (0x7FF9000000000000)
; TAG_PTR_CONST  = 9218868437227405312  (0x7FF8000000000000)
; VAL_TRUE       = 9222246136947933185  (0x7FFA000000000001)
; VAL_FALSE      = 9222246136947933184  (0x7FFA000000000000)
; VAL_NIL        = 9222246136947933186  (0x7FFA000000000002)
; PAYLOAD_MASK   = 281474976710655      (0x0000FFFFFFFFFFFF)

; =============================================================================
; Weak GC Fallbacks
; =============================================================================
; When gc.ll is NOT linked, these weak definitions provide simple malloc-based
; allocation so that the runtime works without a garbage collector.
; When gc.ll IS linked, its strong definitions override these.

declare i8* @calloc(i64, i64)
declare i8* @realloc(i8*, i64)
declare void @free(i8*)

; __gc_alloc: allocate size bytes, return user pointer.
; Weak fallback just calls malloc (no header, no tracking).
define weak i64 @__gc_alloc(i64 %size, i64 %type_tag) {
entry:
  %ptr = call i8* @malloc(i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_alloc_zeroed: allocate zeroed memory.
define weak i64 @__gc_alloc_zeroed(i64 %size, i64 %type_tag) {
entry:
  %ptr = call i8* @calloc(i64 1, i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_realloc: reallocate memory, copying old data.
define weak i64 @__gc_realloc(i64 %old_ptr, i64 %new_size, i64 %type_tag) {
entry:
  %old_p = inttoptr i64 %old_ptr to i8*
  %new_p = call i8* @realloc(i8* %old_p, i64 %new_size)
  %is_null = icmp eq i8* %new_p, null
  br i1 %is_null, label %fail, label %ok
ok:
  %r = ptrtoint i8* %new_p to i64
  ret i64 %r
fail:
  ret i64 %old_ptr
}

; __gc_enable / __gc_disable: no-ops when GC is not linked.
define weak void @__gc_enable() {
entry:
  ret void
}

define weak void @__gc_disable() {
entry:
  ret void
}

; __gc_collect: no-op when GC is not linked.
define weak void @__gc_collect() {
entry:
  ret void
}

; __gc_set_threshold: no-op.
define weak void @__gc_set_threshold(i64 %bytes) {
entry:
  ret void
}

; __gc_push_root / __gc_pop_roots: no-ops without GC.
define weak void @__gc_push_root(i64 %root_addr) {
entry:
  ret void
}

define weak void @__gc_pop_roots(i64 %n) {
entry:
  ret void
}

define weak i64 @__gc_shadow_stack_depth() {
entry:
  ret i64 0
}

; Statistics: all return 0 without GC.
define weak i64 @__gc_stat_alloc_count() {
entry:
  ret i64 0
}

define weak i64 @__gc_stat_total_bytes() {
entry:
  ret i64 0
}

define weak i64 @__gc_stat_collections() {
entry:
  ret i64 0
}

define weak i64 @__gc_stat_freed_bytes() {
entry:
  ret i64 0
}

; __gc_list_new: allocate a list using plain malloc.
define weak i64 @__gc_list_new() {
entry:
  ; List: { count@0, capacity@8, data_ptr@16 } = 24 bytes
  %list_raw = call i8* @calloc(i64 1, i64 24)
  %list = ptrtoint i8* %list_raw to i64
  %is_null = icmp eq i64 %list, 0
  br i1 %is_null, label %done, label %init
init:
  ; count = 0 (already zeroed by calloc)
  ; capacity = 8
  %cap_addr = add i64 %list, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 8, i64* %cap_ptr
  ; data = malloc(64) for 8 slots
  %data_raw = call i8* @calloc(i64 1, i64 64)
  %data = ptrtoint i8* %data_raw to i64
  %data_addr = add i64 %list, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  store i64 %data, i64* %data_ptr
  br label %done
done:
  ret i64 %list
}

; __gc_list_push: push a value onto a list using plain realloc.
define weak void @__gc_list_push(i64 %list, i64 %value) {
entry:
  %is_null = icmp eq i64 %list, 0
  br i1 %is_null, label %done, label %check
check:
  %count_ptr = inttoptr i64 %list to i64*
  %count = load i64, i64* %count_ptr
  %cap_addr = add i64 %list, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  %cap = load i64, i64* %cap_ptr
  %need_grow = icmp uge i64 %count, %cap
  br i1 %need_grow, label %grow, label %store
grow:
  %new_cap = shl i64 %cap, 1
  %new_bytes = shl i64 %new_cap, 3
  %data_addr_g = add i64 %list, 16
  %data_ptr_g = inttoptr i64 %data_addr_g to i64*
  %old_data = load i64, i64* %data_ptr_g
  %old_data_p = inttoptr i64 %old_data to i8*
  %new_data_p = call i8* @realloc(i8* %old_data_p, i64 %new_bytes)
  %new_data = ptrtoint i8* %new_data_p to i64
  store i64 %new_cap, i64* %cap_ptr
  store i64 %new_data, i64* %data_ptr_g
  br label %store
store:
  %data_addr_s = add i64 %list, 16
  %data_ptr_s = inttoptr i64 %data_addr_s to i64*
  %data = load i64, i64* %data_ptr_s
  %offset = shl i64 %count, 3
  %slot = add i64 %data, %offset
  %slot_ptr = inttoptr i64 %slot to i64*
  store i64 %value, i64* %slot_ptr
  %new_count = add i64 %count, 1
  store i64 %new_count, i64* %count_ptr
  br label %done
done:
  ret void
}

; __gc_map_new: allocate a map using plain malloc.
define weak i64 @__gc_map_new() {
entry:
  ; Map: { count@0, capacity@8, keys_ptr@16, values_ptr@24 } = 32 bytes
  %map_raw = call i8* @calloc(i64 1, i64 32)
  %map = ptrtoint i8* %map_raw to i64
  %is_null = icmp eq i64 %map, 0
  br i1 %is_null, label %done, label %init
init:
  ; capacity = 16
  %cap_addr = add i64 %map, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 16, i64* %cap_ptr
  ; keys = calloc(128)
  %keys_raw = call i8* @calloc(i64 1, i64 128)
  %keys = ptrtoint i8* %keys_raw to i64
  %keys_addr = add i64 %map, 16
  %keys_ptr = inttoptr i64 %keys_addr to i64*
  store i64 %keys, i64* %keys_ptr
  ; values = calloc(128)
  %vals_raw = call i8* @calloc(i64 1, i64 128)
  %vals = ptrtoint i8* %vals_raw to i64
  %vals_addr = add i64 %map, 24
  %vals_ptr = inttoptr i64 %vals_addr to i64*
  store i64 %vals, i64* %vals_ptr
  br label %done
done:
  ret i64 %map
}

; __gc_stringbuilder_new: allocate a StringBuilder using plain malloc.
define weak i64 @__gc_stringbuilder_new() {
entry:
  %sb_raw = call i8* @calloc(i64 1, i64 24)
  %sb = ptrtoint i8* %sb_raw to i64
  %is_null = icmp eq i64 %sb, 0
  br i1 %is_null, label %done, label %init
init:
  ; cap = 1024
  %cap_addr = add i64 %sb, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 1024, i64* %cap_ptr
  ; buf = calloc(1024)
  %buf_raw = call i8* @calloc(i64 1, i64 1024)
  %buf = ptrtoint i8* %buf_raw to i64
  %buf_addr = add i64 %sb, 16
  %buf_ptr = inttoptr i64 %buf_addr to i64*
  store i64 %buf, i64* %buf_ptr
  br label %done
done:
  ret i64 %sb
}

; __gc_string_alloc: allocate a string buffer.
define weak i64 @__gc_string_alloc(i64 %size) {
entry:
  %ptr = call i8* @malloc(i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_closure_new: allocate a closure pair.
define weak i64 @__gc_closure_new(i64 %fn_ptr, i64 %env_ptr) {
entry:
  %raw_p = call i8* @malloc(i64 16)
  %raw = ptrtoint i8* %raw_p to i64
  %is_null = icmp eq i64 %raw, 0
  br i1 %is_null, label %done, label %init
init:
  %fn_slot = inttoptr i64 %raw to i64*
  store i64 %fn_ptr, i64* %fn_slot
  %env_addr = add i64 %raw, 8
  %env_slot = inttoptr i64 %env_addr to i64*
  store i64 %env_ptr, i64* %env_slot
  br label %done
done:
  ret i64 %raw
}

; __gc_env_alloc: allocate a closure environment (zeroed).
define weak i64 @__gc_env_alloc(i64 %num_slots) {
entry:
  %size = shl i64 %num_slots, 3
  %ptr = call i8* @calloc(i64 1, i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_instance_alloc: allocate a class instance (zeroed).
define weak i64 @__gc_instance_alloc(i64 %num_fields) {
entry:
  %size = shl i64 %num_fields, 3
  %ptr = call i8* @calloc(i64 1, i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_init_shadow_stack: no-op without GC.
define weak void @__gc_init_shadow_stack() {
entry:
  ret void
}


