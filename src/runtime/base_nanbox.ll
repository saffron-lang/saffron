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
declare i64 @sf_tcp_poll(i64, i64, i64)
declare void @__sched_coro_resume(i64)
declare i64 @__sched_coro_done(i64)
declare void @__sched_coro_destroy(i64)
declare i64 @__sched_get_yield_reason()
declare i64 @__sched_get_yield_arg()
declare i64 @__sched_get_task_result()
declare void @__sched_reset_yield()
declare void @__sched_store_result(i64, i64)
declare i64 @__sched_get_stored_result(i64)
declare i64 @__sched_has_stored_result(i64)

; --- Typed IO dispatch (compile-time polymorphism) ---
; All values are i64. The codegen picks the right variant at compile time.

declare i32 @puts(i8*)
declare i32 @printf(i8*, ...)
declare i32 @fflush(i8*)
declare void @exit(i32)

@.fmt.ld_nl = private unnamed_addr constant [5 x i8] c"%ld\0A\00"
@.fmt.ld = private unnamed_addr constant [4 x i8] c"%ld\00"
@.str.true_nl = private unnamed_addr constant [5 x i8] c"true\00"
@.str.false_nl = private unnamed_addr constant [6 x i8] c"false\00"
@.str.nil_nl = private unnamed_addr constant [4 x i8] c"nil\00"
@.str.pct_s = private unnamed_addr constant [3 x i8] c"%s\00"

define void @__io_println_str(i64 %s) {
entry:
  %ptr = call i8* @__val_untag_ptr(i64 %s)
  call i32 @puts(i8* %ptr)
  call i32 @fflush(i8* null)
  ret void
}

define void @__io_println_int(i64 %n) {
entry:
  %fmt = getelementptr [5 x i8], [5 x i8]* @.fmt.ld_nl, i64 0, i64 0
  %raw = call i64 @__val_untag_int(i64 %n)
  call i32 (i8*, ...) @printf(i8* %fmt, i64 %raw)
  call i32 @fflush(i8* null)
  ret void
}

define void @__io_println_bool(i64 %b) {
entry:
  %raw = call i64 @__val_untag_bool(i64 %b)
  %is_true = icmp ne i64 %raw, 0
  br i1 %is_true, label %yes, label %no
yes:
  %t = getelementptr [5 x i8], [5 x i8]* @.str.true_nl, i64 0, i64 0
  call i32 @puts(i8* %t)
  call i32 @fflush(i8* null)
  ret void
no:
  %f = getelementptr [6 x i8], [6 x i8]* @.str.false_nl, i64 0, i64 0
  call i32 @puts(i8* %f)
  call i32 @fflush(i8* null)
  ret void
}

define void @__io_println_nil() {
entry:
  %s = getelementptr [4 x i8], [4 x i8]* @.str.nil_nl, i64 0, i64 0
  call i32 @puts(i8* %s)
  call i32 @fflush(i8* null)
  ret void
}

define void @__io_print_str(i64 %s) {
entry:
  %ptr = call i8* @__val_untag_ptr(i64 %s)
  %fmt = getelementptr [3 x i8], [3 x i8]* @.str.pct_s, i64 0, i64 0
  call i32 (i8*, ...) @printf(i8* %fmt, i8* %ptr)
  ret void
}

define void @__io_print_int(i64 %n) {
entry:
  %fmt = getelementptr [4 x i8], [4 x i8]* @.fmt.ld, i64 0, i64 0
  %raw = call i64 @__val_untag_int(i64 %n)
  call i32 (i8*, ...) @printf(i8* %fmt, i64 %raw)
  ret void
}

; __io_println — Universal println: delegates to __io_println_any which handles
; all NaN-boxed types (int, bool, nil, ptr/string, float) via tag dispatch.
define i64 @__io_println(i64 %val) {
entry:
  call void @__io_println_any(i64 %val)
  ret i64 0
}

; __io_print — Universal print (no newline): converts via __any_to_string then printf.
define i64 @__io_print(i64 %val) {
entry:
  %str = call i64 @__any_to_string(i64 %val)
  %str_ptr = inttoptr i64 %str to i8*
  %fmt = getelementptr [3 x i8], [3 x i8]* @.str.pct_s, i64 0, i64 0
  call i32 (i8*, ...) @printf(i8* %fmt, i8* %str_ptr)
  ret i64 0
}

; __os_exit — Exit process with given code (truncated to i32).
define i64 @__os_exit(i64 %code) {
entry:
  %code32 = trunc i64 %code to i32
  call void @exit(i32 %code32)
  ret i64 0
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
; ACTIVE NaN-boxing implementations.
; TAG_INT  = 0x7FF9000000000000 = 9221401712017801216
; TAG_PTR  = 0x7FF8000000000000 = 9221120237041090560
; VAL_TRUE  = 0x7FFA000000000001 = 9221683186994511873
; VAL_FALSE = 0x7FFA000000000000 = 9221683186994511872
; VAL_NIL   = 0x7FFA000000000002 = 9221683186994511874
; PAYLOAD_MASK = 0x0000FFFFFFFFFFFF = 281474976710655

define i64 @__val_tag_int(i64 %n) {
entry:
  %masked = and i64 %n, 281474976710655
  %tagged = or i64 %masked, 9221401712017801216
  ret i64 %tagged
}

define i64 @__val_untag_int(i64 %v) {
entry:
  %payload = and i64 %v, 281474976710655
  %shift_left = shl i64 %payload, 16
  %sign_ext = ashr i64 %shift_left, 16
  ret i64 %sign_ext
}

define i64 @__val_tag_ptr(i8* %ptr) {
entry:
  %int_ptr = ptrtoint i8* %ptr to i64
  %masked = and i64 %int_ptr, 281474976710655
  %tagged = or i64 %masked, 9221120237041090560
  ret i64 %tagged
}

define i8* @__val_untag_ptr(i64 %v) {
entry:
  %ptr_int = and i64 %v, 281474976710655
  %ptr = inttoptr i64 %ptr_int to i8*
  ret i8* %ptr
}

define i64 @__val_tag_float(double %f) {
entry:
  %bits = bitcast double %f to i64
  ret i64 %bits
}

define double @__val_untag_float(i64 %v) {
entry:
  ; Check if value has int tag (top 16 bits == 0x7FF9)
  %tag_bits = lshr i64 %v, 48
  %is_int = icmp eq i64 %tag_bits, 32761
  br i1 %is_int, label %convert_int, label %as_float
convert_int:
  ; Extract int payload (sign-extend from 48 bits) and convert to double
  %payload = and i64 %v, 281474976710655
  %shift_left = shl i64 %payload, 16
  %sign_ext = ashr i64 %shift_left, 16
  %from_int = sitofp i64 %sign_ext to double
  ret double %from_int
as_float:
  %f = bitcast i64 %v to double
  ret double %f
}

define i64 @__val_tag_bool(i64 %b) {
entry:
  %is_true = icmp ne i64 %b, 0
  %result = select i1 %is_true, i64 9221683186994511873, i64 9221683186994511872
  ret i64 %result
}

define i64 @__val_untag_bool(i64 %v) {
entry:
  %is_true = icmp eq i64 %v, 9221683186994511873
  %result = zext i1 %is_true to i64
  ret i64 %result
}

define i64 @__val_nil() {
entry:
  ret i64 9221683186994511874
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
  %result = icmp eq i64 %v, 9221683186994511874 ; 0x7FFA000000000002
  ret i1 %result
}

define i1 @__val_is_true(i64 %v) {
entry:
  ; true = 0x7FFA000000000001
  %result = icmp eq i64 %v, 9221683186994511873 ; 0x7FFA000000000001
  ret i1 %result
}

define i1 @__val_is_bool(i64 %v) {
entry:
  %is_t = icmp eq i64 %v, 9221683186994511873  ; true  (0x7FFA000000000001)
  %is_f = icmp eq i64 %v, 9221683186994511872  ; false (0x7FFA000000000000)
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
; TAG_INT_CONST  = 9221401712017801216  (0x7FF9000000000000)
; TAG_PTR_CONST  = 9221120237041090560  (0x7FF8000000000000)
; VAL_TRUE       = 9221683186994511873  (0x7FFA000000000001)
; VAL_FALSE      = 9221683186994511872  (0x7FFA000000000000)
; VAL_NIL        = 9221683186994511874  (0x7FFA000000000002)
; PAYLOAD_MASK   = 281474976710655      (0x0000FFFFFFFFFFFF)

; =============================================================================
; String Interning — O(1) Equality via Pointer Comparison
; =============================================================================
;
; __string_eq: Fast string equality check.
;   1. Pointer equality (O(1) — same literal or same interned string)
;   2. Falls back to strcmp for dynamically-created strings
;
; In NaN-boxing mode, args are tagged pointers — untag before comparing.
; Returns 1 (equal) or 0 (not equal) as i64.

declare i32 @strcmp(i8*, i8*)

define i64 @__string_eq(i64 %a, i64 %b) {
entry:
  ; Fast path: same tagged value = same string
  %same = icmp eq i64 %a, %b
  br i1 %same, label %equal, label %slow

slow:
  ; Untag both pointers
  %a_raw = call i8* @__val_untag_ptr(i64 %a)
  %b_raw = call i8* @__val_untag_ptr(i64 %b)

  ; Null checks: if either is null, they're not equal
  %a_null = icmp eq i8* %a_raw, null
  br i1 %a_null, label %not_equal, label %check_b

check_b:
  %b_null = icmp eq i8* %b_raw, null
  br i1 %b_null, label %not_equal, label %do_strcmp

do_strcmp:
  ; Fall back to byte-by-byte comparison
  %cmp = call i32 @strcmp(i8* %a_raw, i8* %b_raw)
  %is_eq = icmp eq i32 %cmp, 0
  br i1 %is_eq, label %equal, label %not_equal

equal:
  ret i64 1
not_equal:
  ret i64 0
}

; __string_ne: Fast string inequality (complement of __string_eq)
define i64 @__string_ne(i64 %a, i64 %b) {
entry:
  %eq = call i64 @__string_eq(i64 %a, i64 %b)
  %ne = xor i64 %eq, 1
  ret i64 %ne
}

; =============================================================================
; __any_eq — Deep equality for Any-typed values
; =============================================================================
; When both operands are typed as Any, raw i64 equality (icmp eq) is wrong for
; heap-allocated strings: two strings with the same content but different
; allocations would compare unequal. This function checks NaN-box tags and
; dispatches to strcmp for pointer-tagged values that are strings.
;
; Returns 1 (equal) or 0 (not equal) as i64.
define i64 @__any_eq(i64 %a, i64 %b) {
entry:
  ; Fast path: bitwise-identical values are always equal
  %same = icmp eq i64 %a, %b
  br i1 %same, label %equal, label %check_tags

check_tags:
  ; Check if both are pointer-tagged (upper 16 bits == 0x7FF8)
  %a_upper = lshr i64 %a, 48
  %b_upper = lshr i64 %b, 48
  %a_is_ptr = icmp eq i64 %a_upper, 32760
  %b_is_ptr = icmp eq i64 %b_upper, 32760
  %both_ptr = and i1 %a_is_ptr, %b_is_ptr
  br i1 %both_ptr, label %ptr_compare, label %not_equal

ptr_compare:
  ; Both are pointer-tagged — do strcmp (works for strings; for non-string
  ; heap objects with same content this is a safe conservative comparison
  ; since different object types will have different byte prefixes)
  %a_raw = call i8* @__val_untag_ptr(i64 %a)
  %b_raw = call i8* @__val_untag_ptr(i64 %b)
  %a_null = icmp eq i8* %a_raw, null
  br i1 %a_null, label %not_equal, label %check_b_null

check_b_null:
  %b_null = icmp eq i8* %b_raw, null
  br i1 %b_null, label %not_equal, label %do_strcmp

do_strcmp:
  %cmp = call i32 @strcmp(i8* %a_raw, i8* %b_raw)
  %is_eq = icmp eq i32 %cmp, 0
  br i1 %is_eq, label %equal, label %not_equal

equal:
  ret i64 1
not_equal:
  ret i64 0
}

; __any_ne: Deep inequality for Any-typed values (complement of __any_eq)
define i64 @__any_ne(i64 %a, i64 %b) {
entry:
  %eq = call i64 @__any_eq(i64 %a, i64 %b)
  %ne = xor i64 %eq, 1
  ret i64 %ne
}

; =============================================================================
; String Interning — Phase 2: Intern Table for Dynamic Strings
; =============================================================================
;
; Hash table mapping string content → canonical pointer.
; After interning, all strings with equal content share the same pointer,
; making __string_eq O(1) via the fast-path pointer check.
;
; Table layout:
;   @__intern_buckets: pointer to array of bucket head pointers (linked list)
;   @__intern_bucket_count: number of buckets (initially 1024)
;   @__intern_size: number of interned entries
;
; Each bucket entry (node): { i64 str_ptr, i64 next_node_ptr }
;   - str_ptr: the canonical interned string pointer
;   - next_node_ptr: pointer to next node in bucket chain (0 = end)
;
; Hash: FNV-1a 64-bit on string bytes

@__intern_buckets = global i8* null
@__intern_bucket_count = global i64 1024
@__intern_size = global i64 0

; __intern_init: lazily initialize the intern table
define internal void @__intern_init() {
entry:
  %buckets = load i8*, i8** @__intern_buckets
  %is_null = icmp eq i8* %buckets, null
  br i1 %is_null, label %do_init, label %done

do_init:
  ; Allocate 1024 buckets * 8 bytes each = 8192 bytes, zeroed
  %mem = call i8* @calloc(i64 1024, i64 8)
  store i8* %mem, i8** @__intern_buckets
  br label %done

done:
  ret void
}

; __string_hash: FNV-1a 64-bit hash of a null-terminated string
; Returns hash as i64.
define i64 @__string_hash(i64 %str_ptr) {
entry:
  %ptr = inttoptr i64 %str_ptr to i8*
  ; FNV offset basis: 14695981039346656037
  br label %loop

loop:
  %hash = phi i64 [ -3750763034362895579, %entry ], [ %new_hash, %next ]
  %p = phi i8* [ %ptr, %entry ], [ %p_next, %next ]
  %byte = load i8, i8* %p
  %is_zero = icmp eq i8 %byte, 0
  br i1 %is_zero, label %done, label %next

next:
  %byte_ext = zext i8 %byte to i64
  %xored = xor i64 %hash, %byte_ext
  ; FNV prime: 1099511628211
  %new_hash = mul i64 %xored, 1099511628211
  %p_next = getelementptr i8, i8* %p, i64 1
  br label %loop

done:
  %final = phi i64 [ %hash, %loop ]
  ret i64 %final
}

; __string_intern: Intern a string pointer.
;   If a string with the same content already exists in the table, return
;   the existing pointer (and free the duplicate).
;   Otherwise, add this pointer to the table and return it.
define i64 @__string_intern(i64 %str) {
entry:
  ; Null check
  %is_null = icmp eq i64 %str, 0
  br i1 %is_null, label %ret_input, label %do_intern

do_intern:
  ; Ensure table is initialized
  call void @__intern_init()

  ; Hash the string
  %hash = call i64 @__string_hash(i64 %str)

  ; Compute bucket index: hash & (bucket_count - 1)
  %bucket_count = load i64, i64* @__intern_bucket_count
  %mask = sub i64 %bucket_count, 1
  %idx = and i64 %hash, %mask

  ; Get bucket array base
  %buckets_raw = load i8*, i8** @__intern_buckets
  %buckets = bitcast i8* %buckets_raw to i64*

  ; Get head of this bucket's chain
  %bucket_slot = getelementptr i64, i64* %buckets, i64 %idx
  %head = load i64, i64* %bucket_slot
  br label %search

search:
  ; Walk the linked list looking for a match
  %node = phi i64 [ %head, %do_intern ], [ %next_node, %continue_search ]
  %node_is_null = icmp eq i64 %node, 0
  br i1 %node_is_null, label %not_found, label %check_node

check_node:
  ; Node layout: { i64 str_ptr, i64 next }
  %node_ptr = inttoptr i64 %node to i64*
  %existing_str = load i64, i64* %node_ptr
  ; Quick check: same pointer means same string
  %same_ptr = icmp eq i64 %existing_str, %str
  br i1 %same_ptr, label %found, label %cmp_content

cmp_content:
  ; Compare content via strcmp
  %str_p = inttoptr i64 %str to i8*
  %existing_p = inttoptr i64 %existing_str to i8*
  %cmp = call i32 @strcmp(i8* %str_p, i8* %existing_p)
  %is_eq = icmp eq i32 %cmp, 0
  br i1 %is_eq, label %found_free, label %continue_search

continue_search:
  ; Move to next node
  %next_addr = add i64 %node, 8
  %next_ptr = inttoptr i64 %next_addr to i64*
  %next_node = load i64, i64* %next_ptr
  br label %search

found_free:
  ; Found an existing string with the same content.
  ; Free the new duplicate and return the canonical one.
  %dup_ptr = inttoptr i64 %str to i8*
  call void @free(i8* %dup_ptr)
  br label %found

found:
  %canonical = phi i64 [ %str, %check_node ], [ %existing_str, %found_free ]
  ret i64 %canonical

not_found:
  ; Not in table — add it. Allocate a node (16 bytes: str_ptr + next)
  %new_node_raw = call i8* @malloc(i64 16)
  %new_node = ptrtoint i8* %new_node_raw to i64
  %new_node_str = inttoptr i64 %new_node to i64*
  store i64 %str, i64* %new_node_str
  ; Point new node's next to the old head
  %new_node_next_addr = add i64 %new_node, 8
  %new_node_next = inttoptr i64 %new_node_next_addr to i64*
  store i64 %head, i64* %new_node_next
  ; Update bucket head
  store i64 %new_node, i64* %bucket_slot
  ; Increment size
  %old_size = load i64, i64* @__intern_size
  %new_size = add i64 %old_size, 1
  store i64 %new_size, i64* @__intern_size
  ; Check load factor: if size > bucket_count * 3/4, rehash
  %threshold = mul i64 %bucket_count, 3
  %threshold4 = udiv i64 %threshold, 4
  %need_rehash = icmp ugt i64 %new_size, %threshold4
  br i1 %need_rehash, label %do_rehash, label %ret_input

do_rehash:
  call void @__intern_rehash()
  br label %ret_input

ret_input:
  ret i64 %str
}

; __intern_rehash: Double the bucket count and redistribute entries.
define internal void @__intern_rehash() {
entry:
  %old_count = load i64, i64* @__intern_bucket_count
  %new_count = shl i64 %old_count, 1
  store i64 %new_count, i64* @__intern_bucket_count

  ; Allocate new bucket array (zeroed)
  %new_buckets_raw = call i8* @calloc(i64 %new_count, i64 8)
  %new_buckets = bitcast i8* %new_buckets_raw to i64*

  ; Walk old buckets and re-insert each node
  %old_buckets_raw = load i8*, i8** @__intern_buckets
  %old_buckets = bitcast i8* %old_buckets_raw to i64*
  %new_mask = sub i64 %new_count, 1
  br label %bucket_loop

bucket_loop:
  %i = phi i64 [ 0, %entry ], [ %i_next, %bucket_done ]
  %done = icmp uge i64 %i, %old_count
  br i1 %done, label %finish, label %process_bucket

process_bucket:
  %slot = getelementptr i64, i64* %old_buckets, i64 %i
  %chain = load i64, i64* %slot
  br label %node_loop

node_loop:
  %node = phi i64 [ %chain, %process_bucket ], [ %saved_next, %reinsert ]
  %node_null = icmp eq i64 %node, 0
  br i1 %node_null, label %bucket_done, label %reinsert

reinsert:
  ; Read node fields
  %n_ptr = inttoptr i64 %node to i64*
  %n_str = load i64, i64* %n_ptr
  %n_next_addr = add i64 %node, 8
  %n_next_ptr = inttoptr i64 %n_next_addr to i64*
  %saved_next = load i64, i64* %n_next_ptr

  ; Compute new bucket index
  %h = call i64 @__string_hash(i64 %n_str)
  %new_idx = and i64 %h, %new_mask
  %new_slot = getelementptr i64, i64* %new_buckets, i64 %new_idx
  %old_head = load i64, i64* %new_slot

  ; Insert node at head of new chain
  store i64 %old_head, i64* %n_next_ptr
  store i64 %node, i64* %new_slot
  br label %node_loop

bucket_done:
  %i_next = add i64 %i, 1
  br label %bucket_loop

finish:
  ; Free old bucket array, install new one
  call void @free(i8* %old_buckets_raw)
  %new_buckets_i8 = bitcast i64* %new_buckets to i8*
  store i8* %new_buckets_i8, i8** @__intern_buckets
  ret void
}

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
define weak i64 @__gc_enable() {
entry:
  ret i64 0
}

define weak i64 @__gc_disable() {
entry:
  ret i64 0
}

; __gc_collect: no-op when GC is not linked.
define weak i64 @__gc_collect() {
entry:
  ret i64 0
}

; __gc_set_threshold: no-op.
define weak i64 @__gc_set_threshold(i64 %bytes) {
entry:
  ret i64 0
}

; __gc_debug_stats: no-op when GC is not linked.
define weak i64 @__gc_debug_stats() {
entry:
  ret i64 0
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

; =============================================================================
; __any_to_string — Runtime type dispatch for NaN-boxed values
; =============================================================================
; Takes a NaN-boxed i64, determines its type at runtime, and returns a raw
; char* (as i64) pointing to the string representation.

declare i64 @__int_to_string(i64)

define i64 @__any_to_string(i64 %val) {
entry:
  ; Check nil first
  %is_nil = call i1 @__val_is_nil(i64 %val)
  br i1 %is_nil, label %ret_nil, label %check_bool

check_bool:
  %is_true = call i1 @__val_is_true(i64 %val)
  br i1 %is_true, label %ret_true, label %check_false

check_false:
  ; Check false (0x7FFA000000000000)
  %is_false = icmp eq i64 %val, 9221683186994511872
  br i1 %is_false, label %ret_false, label %check_int

check_int:
  %is_int = call i1 @__val_is_int(i64 %val)
  br i1 %is_int, label %do_int, label %check_ptr

do_int:
  %raw_int = call i64 @__val_untag_int(i64 %val)
  %int_str = call i64 @__int_to_string(i64 %raw_int)
  ret i64 %int_str

check_ptr:
  %is_ptr = call i1 @__val_is_ptr(i64 %val)
  br i1 %is_ptr, label %do_ptr, label %do_float

do_ptr:
  ; Untag the pointer — caller (puts) expects a raw i8*
  %raw_ptr = call i8* @__val_untag_ptr(i64 %val)
  %ptr_as_i64 = ptrtoint i8* %raw_ptr to i64
  ret i64 %ptr_as_i64

do_float:
  ; Must be a float (not NaN-tagged)
  %float_str = call i64 @__float_to_string(i64 %val)
  ret i64 %float_str

ret_nil:
  %nil_str = getelementptr [4 x i8], [4 x i8]* @.str.nil_nl, i64 0, i64 0
  %nil_ptr = ptrtoint i8* %nil_str to i64
  ret i64 %nil_ptr

ret_true:
  %true_str = getelementptr [5 x i8], [5 x i8]* @.str.true_nl, i64 0, i64 0
  %true_ptr = ptrtoint i8* %true_str to i64
  ret i64 %true_ptr

ret_false:
  %false_str = getelementptr [6 x i8], [6 x i8]* @.str.false_nl, i64 0, i64 0
  %false_ptr = ptrtoint i8* %false_str to i64
  ret i64 %false_ptr
}

; __io_println_any — Print any NaN-boxed value followed by a newline.
define void @__io_println_any(i64 %val) {
entry:
  %str = call i64 @__any_to_string(i64 %val)
  %ptr = inttoptr i64 %str to i8*
  call i32 @puts(i8* %ptr)
  ret void
}

; =============================================================================
; Debug Location Tracking
; =============================================================================

; Debug location tracking (set by codegen before potentially-crashing operations)
@__debug_location = global i8* null

; Print debug location to stderr if set (called by runtime on error)
@.debug.at = private unnamed_addr constant [6 x i8] c"  at \00"
@.debug.nl = private unnamed_addr constant [2 x i8] c"\0A\00"

declare i64 @strlen(i8*)
declare i64 @write(i32, i8*, i64)

define void @__print_debug_location() {
entry:
  %loc = load i8*, i8** @__debug_location
  %is_null = icmp eq i8* %loc, null
  br i1 %is_null, label %done, label %check_empty
check_empty:
  %first = load i8, i8* %loc
  %is_empty = icmp eq i8 %first, 0
  br i1 %is_empty, label %done, label %print
print:
  %at_ptr = getelementptr [6 x i8], [6 x i8]* @.debug.at, i64 0, i64 0
  call i64 @write(i32 2, i8* %at_ptr, i64 5)
  %len = call i64 @strlen(i8* %loc)
  call i64 @write(i32 2, i8* %loc, i64 %len)
  %nl_ptr = getelementptr [2 x i8], [2 x i8]* @.debug.nl, i64 0, i64 0
  call i64 @write(i32 2, i8* %nl_ptr, i64 1)
  br label %done
done:
  ret void
}

