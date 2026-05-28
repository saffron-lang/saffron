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
