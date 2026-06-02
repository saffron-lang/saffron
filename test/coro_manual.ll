; LLVM coroutine intrinsics validation test
; Uses presplitcoroutine keyword + 2-arg coro.end (works on Apple + Homebrew clang)
;
; Expected output:
;   before yield
;   resumed once
;   after yield
;   done

target triple = "arm64-apple-macosx15.0.0"

declare i32 @puts(ptr)
declare ptr @malloc(i64)
declare void @free(ptr)

@.str.before = private unnamed_addr constant [13 x i8] c"before yield\00"
@.str.after = private unnamed_addr constant [12 x i8] c"after yield\00"
@.str.resumed = private unnamed_addr constant [13 x i8] c"resumed once\00"
@.str.done = private unnamed_addr constant [5 x i8] c"done\00"

define ptr @my_coroutine() presplitcoroutine {
entry:
  %id = call token @llvm.coro.id(i32 0, ptr null, ptr null, ptr null)
  %size = call i64 @llvm.coro.size.i64()
  %mem = call ptr @malloc(i64 %size)
  %hdl = call ptr @llvm.coro.begin(token %id, ptr %mem)

  call i32 @puts(ptr @.str.before)

  ; --- Yield point ---
  %s1 = call token @llvm.coro.save(ptr %hdl)
  %r1 = call i8 @llvm.coro.suspend(token %s1, i1 false)
  switch i8 %r1, label %suspend [
    i8 0, label %resume
    i8 1, label %cleanup
  ]

resume:
  call i32 @puts(ptr @.str.after)

  ; --- Final suspend ---
  %s2 = call token @llvm.coro.save(ptr %hdl)
  %r2 = call i8 @llvm.coro.suspend(token %s2, i1 true)
  switch i8 %r2, label %suspend [
    i8 0, label %trap
    i8 1, label %cleanup
  ]

trap:
  unreachable

cleanup:
  %mem2 = call ptr @llvm.coro.free(token %id, ptr %hdl)
  call void @free(ptr %mem2)
  br label %suspend

suspend:
  call i1 @llvm.coro.end(ptr %hdl, i1 false)
  ret ptr %hdl
}

define i32 @main() {
entry:
  ; Start coroutine — runs until first suspend
  %hdl = call ptr @my_coroutine()

  call i32 @puts(ptr @.str.resumed)

  ; Resume — runs to final suspend
  call void @llvm.coro.resume(ptr %hdl)

  ; Check done
  %done = call i1 @llvm.coro.done(ptr %hdl)
  br i1 %done, label %yes, label %no

yes:
  call i32 @puts(ptr @.str.done)
  call void @llvm.coro.destroy(ptr %hdl)
  ret i32 0

no:
  ret i32 1
}

declare token @llvm.coro.id(i32, ptr, ptr, ptr)
declare i64 @llvm.coro.size.i64()
declare ptr @llvm.coro.begin(token, ptr)
declare token @llvm.coro.save(ptr)
declare i8 @llvm.coro.suspend(token, i1)
declare ptr @llvm.coro.free(token, ptr)
declare i1 @llvm.coro.end(ptr, i1)
declare void @llvm.coro.resume(ptr)
declare i1 @llvm.coro.done(ptr)
declare void @llvm.coro.destroy(ptr)
