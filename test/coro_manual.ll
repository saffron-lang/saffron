; Manual LLVM coroutine test
; Validates that the coro pass pipeline works with our clang.
;
; Expected output:
;   before yield
;   resumed once
;   after yield
;   done

target triple = "arm64-apple-macosx15.0.0"

declare i32 @puts(i8*)
declare i8* @malloc(i64)
declare void @free(i8*)

@.str.before = private unnamed_addr constant [13 x i8] c"before yield\00"
@.str.after = private unnamed_addr constant [12 x i8] c"after yield\00"
@.str.resumed = private unnamed_addr constant [13 x i8] c"resumed once\00"
@.str.done = private unnamed_addr constant [5 x i8] c"done\00"

define i8* @my_coroutine() #0 {
entry:
  %id = call token @llvm.coro.id(i32 0, i8* null, i8* null, i8* null)
  %need = call i64 @llvm.coro.size.i64()
  %mem = call i8* @malloc(i64 %need)
  %hdl = call i8* @llvm.coro.begin(token %id, i8* %mem)

  ; Print "before yield"
  %s1 = getelementptr [13 x i8], [13 x i8]* @.str.before, i64 0, i64 0
  call i32 @puts(i8* %s1)

  ; --- Suspend (yield) ---
  %tok1 = call token @llvm.coro.save(i8* %hdl)
  %susp1 = call i8 @llvm.coro.suspend(token %tok1, i1 false)
  switch i8 %susp1, label %suspend [
    i8 0, label %resume
    i8 1, label %cleanup
  ]

resume:
  ; Print "after yield"
  %s2 = getelementptr [12 x i8], [12 x i8]* @.str.after, i64 0, i64 0
  call i32 @puts(i8* %s2)

  ; --- Final suspend ---
  %tok2 = call token @llvm.coro.save(i8* %hdl)
  %susp2 = call i8 @llvm.coro.suspend(token %tok2, i1 true)
  switch i8 %susp2, label %suspend [
    i8 0, label %unreachable_bb
    i8 1, label %cleanup
  ]

unreachable_bb:
  unreachable

cleanup:
  %mem2 = call i8* @llvm.coro.free(token %id, i8* %hdl)
  call void @free(i8* %mem2)
  br label %exit

exit:
  %unused = call i1 @llvm.coro.end(i8* %hdl, i1 false, token none)
  ret i8* %hdl

suspend:
  ret i8* %hdl
}

define i32 @main() {
entry:
  ; Start coroutine — runs until first suspend, returns handle
  %hdl = call i8* @my_coroutine()

  ; Print "resumed once"
  %s3 = getelementptr [13 x i8], [13 x i8]* @.str.resumed, i64 0, i64 0
  call i32 @puts(i8* %s3)

  ; Resume — runs from resume label to final suspend
  call void @llvm.coro.resume(i8* %hdl)

  ; After final suspend, coro.done should return true
  %is_done = call i1 @llvm.coro.done(i8* %hdl)
  br i1 %is_done, label %yes_done, label %not_done

yes_done:
  %s4 = getelementptr [5 x i8], [5 x i8]* @.str.done, i64 0, i64 0
  call i32 @puts(i8* %s4)
  ; Destroy the coroutine (triggers cleanup path)
  call void @llvm.coro.destroy(i8* %hdl)
  ret i32 0

not_done:
  ret i32 1
}

declare token @llvm.coro.id(i32, i8*, i8*, i8*)
declare i64 @llvm.coro.size.i64()
declare i8* @llvm.coro.begin(token, i8*)
declare token @llvm.coro.save(i8*)
declare i8 @llvm.coro.suspend(token, i1)
declare i8* @llvm.coro.free(token, i8*)
declare i1 @llvm.coro.end(i8*, i1, token)
declare void @llvm.coro.resume(i8*)
declare i1 @llvm.coro.done(i8*)
declare void @llvm.coro.destroy(i8*)
