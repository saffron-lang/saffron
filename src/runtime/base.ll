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

