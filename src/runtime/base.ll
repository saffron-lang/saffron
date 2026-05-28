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

define i64 @__nil_to_string() {
entry:
  %s = getelementptr [4 x i8], [4 x i8]* @.str.nil_nl, i64 0, i64 0
  %r = ptrtoint i8* %s to i64
  ret i64 %r
}

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
; Currently IDENTITY (no-op) for backward compatibility.
; When NaN-boxing is activated, replace with real tagging implementations.
; The optimizer inlines these away at -O1+.

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

