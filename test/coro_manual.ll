; Manual coroutine test — pre-split (mimics C++ frontend output)
; Uses the same frame layout as C++ coroutines:
;   frame[0] = resume function pointer
;   frame[1] = destroy function pointer
;   frame[2] = state index (i32 padded to i64 for alignment)
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

; Frame: { ptr resume_fn, ptr destroy_fn, i32 state }
%CoroFrame = type { ptr, ptr, i32 }

; --- Create the coroutine (initial call) ---
; Returns the frame pointer. State starts at 0 (initial suspend).
define ptr @my_coroutine() {
entry:
  %frame = call ptr @malloc(i64 24)
  ; Store resume and destroy function pointers
  %resume_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 0
  store ptr @my_coroutine.resume, ptr %resume_slot
  %destroy_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 1
  store ptr @my_coroutine.destroy, ptr %destroy_slot
  ; State 0 = suspended at initial point
  %state_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 2
  store i32 0, ptr %state_slot
  ret ptr %frame
}

; --- Resume function (called via handle.resume()) ---
define void @my_coroutine.resume(ptr %frame) {
entry:
  %state_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 2
  %state = load i32, ptr %state_slot
  switch i32 %state, label %unreachable [
    i32 0, label %state0
    i32 1, label %state1
  ]

state0:
  ; First resume: print "before yield", suspend at state 1
  call i32 @puts(ptr @.str.before)
  store i32 1, ptr %state_slot
  ret void

state1:
  ; Second resume: print "after yield", mark done (state 2)
  call i32 @puts(ptr @.str.after)
  store i32 2, ptr %state_slot
  ret void

unreachable:
  ret void
}

; --- Destroy function (frees the frame) ---
define void @my_coroutine.destroy(ptr %frame) {
entry:
  call void @free(ptr %frame)
  ret void
}

; --- Helper: check if coroutine is done (state == 2) ---
define i1 @coro_done(ptr %frame) {
entry:
  %state_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 2
  %state = load i32, ptr %state_slot
  %done = icmp eq i32 %state, 2
  ret i1 %done
}

; --- Helper: resume via function pointer in frame ---
define void @coro_resume(ptr %frame) {
entry:
  %fn_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 0
  %fn = load ptr, ptr %fn_slot
  call void %fn(ptr %frame)
  ret void
}

; --- Helper: destroy via function pointer in frame ---
define void @coro_destroy(ptr %frame) {
entry:
  %fn_slot = getelementptr %CoroFrame, ptr %frame, i32 0, i32 1
  %fn = load ptr, ptr %fn_slot
  call void %fn(ptr %frame)
  ret void
}

; --- Main ---
define i32 @main() {
entry:
  %hdl = call ptr @my_coroutine()

  ; First resume: runs state0 (prints "before yield")
  call void @coro_resume(ptr %hdl)

  call i32 @puts(ptr @.str.resumed)

  ; Second resume: runs state1 (prints "after yield")
  call void @coro_resume(ptr %hdl)

  ; Check done
  %done = call i1 @coro_done(ptr %hdl)
  br i1 %done, label %yes, label %no

yes:
  call i32 @puts(ptr @.str.done)
  call void @coro_destroy(ptr %hdl)
  ret i32 0

no:
  ret i32 1
}
