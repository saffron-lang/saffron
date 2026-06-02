target triple = "wasm32-unknown-unknown"
target datalayout = "e-m:e-p:32:32-i64:64-n32:64-S128-ni:1:10:20"

; =============================================================================
; WASM Runtime Base
; Replaces base.ll for wasm32 target. Provides:
;   - Linear memory bump allocator (replaces malloc/realloc/free)
;   - JS-imported I/O (replaces puts/printf)
;   - String operations (strlen, strcmp, strncmp, strcpy, strcat, memcpy)
;   - Exception handling stubs
; =============================================================================

; --- Globals ---

; __heap_base is a linker-provided symbol whose ADDRESS equals the first byte
; after all static data.  We use ptrtoint on it (not a load) to get the value.
@__heap_base = external global i8
@__heap_ptr = global i64 0
@__argc = weak global i32 0
@__argv = weak global i8** null
@__exception_value = weak global i64 0
@__jmp_buf_stack = weak global [64 x i8] zeroinitializer

; --- JS Imports ---
; These are provided by the JS glue code via WebAssembly.imports

declare void @js_log_str(i8*)
declare void @js_log_int(i64)
declare void @js_log_bool(i64)
declare void @js_log_nil()

; --- DOM Operations (provided by JS glue) ---

declare i64 @js_dom_create_element(i8*)
declare void @js_dom_set_text(i64, i8*)
declare void @js_dom_set_attribute(i64, i8*, i8*)
declare void @js_dom_append_child(i64, i64)
declare void @js_dom_remove_child(i64, i64)
declare void @js_dom_set_inner_html(i64, i8*)
declare i64 @js_dom_query_selector(i8*)
declare void @js_dom_add_event_listener(i64, i8*, i64)

; --- Memory Allocator (bump allocator) ---
; Simple and fast. No free. Suitable for short-lived WASM modules.
; For long-running apps, replace with a proper allocator later.

define i8* @malloc(i64 %size) {
entry:
  ; Align to 8 bytes
  %aligned = add i64 %size, 7
  %mask = and i64 %aligned, -8
  ; Load current heap pointer
  %heap = load i64, i64* @__heap_ptr
  %ptr = inttoptr i64 %heap to i8*
  ; Bump heap pointer
  %new_heap = add i64 %heap, %mask
  store i64 %new_heap, i64* @__heap_ptr
  ret i8* %ptr
}

define i8* @realloc(i8* %old_ptr, i64 %new_size) {
entry:
  ; Bump allocator: just allocate new block and copy
  ; (old block is leaked — acceptable for bump allocator)
  %new_ptr = call i8* @malloc(i64 %new_size)
  ; Copy old data (conservatively copy new_size bytes; may over-read but safe in linear memory)
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %new_ptr, i8* %old_ptr, i64 %new_size, i1 false)
  ret i8* %new_ptr
}

define void @free(i8* %ptr) {
entry:
  ; No-op in bump allocator
  ret void
}

; --- String Operations ---

define i64 @strlen(i8* %s) {
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %loop]
  %ptr = getelementptr i8, i8* %s, i64 %i
  %ch = load i8, i8* %ptr
  %done = icmp eq i8 %ch, 0
  %next = add i64 %i, 1
  br i1 %done, label %end, label %loop
end:
  ret i64 %i
}

define i32 @strcmp(i8* %a, i8* %b) {
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %pa = getelementptr i8, i8* %a, i64 %i
  %pb = getelementptr i8, i8* %b, i64 %i
  %ca = load i8, i8* %pa
  %cb = load i8, i8* %pb
  %diff = sub i8 %ca, %cb
  %ne = icmp ne i8 %diff, 0
  br i1 %ne, label %neq, label %cont
cont:
  %az = icmp eq i8 %ca, 0
  %next = add i64 %i, 1
  br i1 %az, label %eq, label %loop
neq:
  %ext = sext i8 %diff to i32
  ret i32 %ext
eq:
  ret i32 0
}

define i32 @strncmp(i8* %a, i8* %b, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %at_end = icmp uge i64 %i, %n
  br i1 %at_end, label %eq, label %body
body:
  %pa = getelementptr i8, i8* %a, i64 %i
  %pb = getelementptr i8, i8* %b, i64 %i
  %ca = load i8, i8* %pa
  %cb = load i8, i8* %pb
  %diff = sub i8 %ca, %cb
  %ne = icmp ne i8 %diff, 0
  br i1 %ne, label %neq, label %cont
cont:
  %az = icmp eq i8 %ca, 0
  %next = add i64 %i, 1
  br i1 %az, label %eq, label %loop
neq:
  %ext = sext i8 %diff to i32
  ret i32 %ext
eq:
  ret i32 0
}

define i8* @strcpy(i8* %dst, i8* %src) {
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %loop]
  %sp = getelementptr i8, i8* %src, i64 %i
  %dp = getelementptr i8, i8* %dst, i64 %i
  %ch = load i8, i8* %sp
  store i8 %ch, i8* %dp
  %done = icmp eq i8 %ch, 0
  %next = add i64 %i, 1
  br i1 %done, label %end, label %loop
end:
  ret i8* %dst
}

define i8* @strcat(i8* %dst, i8* %src) {
entry:
  %dlen = call i64 @strlen(i8* %dst)
  %start = getelementptr i8, i8* %dst, i64 %dlen
  %ignored = call i8* @strcpy(i8* %start, i8* %src)
  ret i8* %dst
}

define i8* @strstr(i8* %haystack, i8* %needle) {
entry:
  %nlen = call i64 @strlen(i8* %needle)
  %nempty = icmp eq i64 %nlen, 0
  br i1 %nempty, label %found_at_zero, label %search
found_at_zero:
  ret i8* %haystack
search:
  %hlen = call i64 @strlen(i8* %haystack)
  br label %loop
loop:
  %i = phi i64 [0, %search], [%next, %nomatch]
  %remain = sub i64 %hlen, %i
  %too_short = icmp ult i64 %remain, %nlen
  br i1 %too_short, label %not_found, label %check
check:
  %hp = getelementptr i8, i8* %haystack, i64 %i
  %cmp = call i32 @strncmp(i8* %hp, i8* %needle, i64 %nlen)
  %match = icmp eq i32 %cmp, 0
  br i1 %match, label %found, label %nomatch
nomatch:
  %next = add i64 %i, 1
  br label %loop
found:
  %result = getelementptr i8, i8* %haystack, i64 %i
  ret i8* %result
not_found:
  ret i8* null
}

declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)

; --- I/O Dispatch ---

define void @__io_println_str(i64 %s) {
entry:
  %ptr = inttoptr i64 %s to i8*
  call void @js_log_str(i8* %ptr)
  ret void
}

define void @__io_println_int(i64 %n) {
entry:
  call void @js_log_int(i64 %n)
  ret void
}

define void @__io_println_bool(i64 %b) {
entry:
  call void @js_log_bool(i64 %b)
  ret void
}

define void @__io_println_nil() {
entry:
  call void @js_log_nil()
  ret void
}

; --- puts (compatibility) ---

define i32 @puts(i8* %s) {
entry:
  call void @js_log_str(i8* %s)
  ret i32 0
}

; --- printf stub (only handles %ld and %s for now) ---

define i32 @printf(i8* %fmt, ...) {
entry:
  ; Minimal stub — just logs the format string
  call void @js_log_str(i8* %fmt)
  ret i32 0
}

; --- snprintf stub (handles %ld only) ---

define i32 @snprintf(i8* %buf, i64 %size, i8* %fmt, ...) {
entry:
  call i8* @strcpy(i8* %buf, i8* %fmt)
  %len = call i64 @strlen(i8* %fmt)
  %len32 = trunc i64 %len to i32
  ret i32 %len32
}

; --- Override __int_to_string for WASM (avoids snprintf) ---

define i64 @__int_to_string(i64 %val) {
entry:
  %buf = call i8* @malloc(i64 24)
  call void @__wasm_int_to_str(i64 %val, i8* %buf)
  %result = ptrtoint i8* %buf to i64
  ret i64 %result
}

; --- Integer to string implementation ---

define void @__wasm_int_to_str(i64 %n, i8* %buf) {
entry:
  %is_neg = icmp slt i64 %n, 0
  br i1 %is_neg, label %neg, label %pos
neg:
  store i8 45, i8* %buf
  %abs_n = sub i64 0, %n
  %buf1 = getelementptr i8, i8* %buf, i64 1
  call void @__wasm_uint_to_str(i64 %abs_n, i8* %buf1)
  ret void
pos:
  call void @__wasm_uint_to_str(i64 %n, i8* %buf)
  ret void
}

define void @__wasm_uint_to_str(i64 %n, i8* %buf) {
entry:
  %is_zero = icmp eq i64 %n, 0
  br i1 %is_zero, label %zero, label %nonzero
zero:
  store i8 48, i8* %buf
  %term0 = getelementptr i8, i8* %buf, i64 1
  store i8 0, i8* %term0
  ret void
nonzero:
  ; Write digits in reverse into temp buffer
  %tmp = alloca [21 x i8]
  %tmp_ptr = getelementptr [21 x i8], [21 x i8]* %tmp, i64 0, i64 0
  br label %loop
loop:
  %val = phi i64 [%n, %nonzero], [%next_val, %loop]
  %idx = phi i64 [0, %nonzero], [%next_idx, %loop]
  %rem = urem i64 %val, 10
  %digit = add i64 %rem, 48
  %digit8 = trunc i64 %digit to i8
  %slot = getelementptr i8, i8* %tmp_ptr, i64 %idx
  store i8 %digit8, i8* %slot
  %next_val = udiv i64 %val, 10
  %next_idx = add i64 %idx, 1
  %done = icmp eq i64 %next_val, 0
  br i1 %done, label %reverse, label %loop
reverse:
  ; Copy reversed digits to output buf
  %len = phi i64 [%next_idx, %loop]
  br label %rev_loop
rev_loop:
  %ri = phi i64 [0, %reverse], [%next_ri, %rev_loop]
  %src_idx = sub i64 %len, %ri
  %src_idx2 = sub i64 %src_idx, 1
  %src_ptr = getelementptr i8, i8* %tmp_ptr, i64 %src_idx2
  %ch = load i8, i8* %src_ptr
  %dst_ptr = getelementptr i8, i8* %buf, i64 %ri
  store i8 %ch, i8* %dst_ptr
  %next_ri = add i64 %ri, 1
  %rev_done = icmp uge i64 %next_ri, %len
  br i1 %rev_done, label %terminate, label %rev_loop
terminate:
  %term = getelementptr i8, i8* %buf, i64 %len
  store i8 0, i8* %term
  ret void
}

; --- atol ---

define i64 @atol(i8* %s) {
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %digit]
  %acc = phi i64 [0, %entry], [%new_acc, %digit]
  %ptr = getelementptr i8, i8* %s, i64 %i
  %ch = load i8, i8* %ptr
  %is_zero = icmp eq i8 %ch, 0
  br i1 %is_zero, label %end, label %check
check:
  %is_digit = icmp uge i8 %ch, 48  ; '0'
  %le_nine = icmp ule i8 %ch, 57   ; '9'
  %valid = and i1 %is_digit, %le_nine
  br i1 %valid, label %digit, label %end
digit:
  %d = sub i8 %ch, 48
  %dext = zext i8 %d to i64
  %shifted = mul i64 %acc, 10
  %new_acc = add i64 %shifted, %dext
  %next = add i64 %i, 1
  br label %loop
end:
  ret i64 %acc
}

; --- File I/O stubs (not available in browser) ---

define i8* @fopen(i8* %path, i8* %mode) {
entry:
  ret i8* null
}

define i64 @fread(i8* %buf, i64 %size, i64 %count, i8* %fp) {
entry:
  ret i64 0
}

define i64 @fwrite(i8* %buf, i64 %size, i64 %count, i8* %fp) {
entry:
  ret i64 0
}

define i32 @fclose(i8* %fp) {
entry:
  ret i32 0
}

define i32 @fseek(i8* %fp, i64 %offset, i32 %whence) {
entry:
  ret i32 -1
}

define i64 @ftell(i8* %fp) {
entry:
  ret i64 0
}

define i8* @fgets(i8* %buf, i32 %size, i8* %fp) {
entry:
  ret i8* null
}

define i8* @strtok(i8* %s, i8* %delim) {
entry:
  ret i8* null
}

define i8* @popen(i8* %cmd, i8* %mode) {
entry:
  ret i8* null
}

define i32 @pclose(i8* %fp) {
entry:
  ret i32 0
}

; --- Exception handling stubs ---

define i32 @setjmp(i8* %buf) {
entry:
  ret i32 0
}

define void @longjmp(i8* %buf, i32 %val) {
entry:
  unreachable
}

; --- exit ---

declare void @__builtin_trap()

define void @exit(i32 %code) {
entry:
  call void @__builtin_trap()
  unreachable
}

; =============================================================================
; NaN-Boxing Value Helpers (identity mode — matches base.ll)
; =============================================================================

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

define i1 @__val_is_float(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760
  %is_int = icmp eq i64 %upper, 32761
  %is_spec = icmp eq i64 %upper, 32762
  %tagged1 = or i1 %is_ptr, %is_int
  %tagged = or i1 %tagged1, %is_spec
  %is_float = xor i1 %tagged, true
  ret i1 %is_float
}

define i1 @__val_is_int(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %r = icmp eq i64 %upper, 32761
  ret i1 %r
}

define i1 @__val_is_ptr(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %r = icmp eq i64 %upper, 32760
  ret i1 %r
}

; --- Entry point wrapper ---
; WASM entry point — initializes heap, then calls __saffron_entry.

declare i64 @__saffron_entry()

define void @_start() {
entry:
  ; Initialize heap pointer from linker-provided __heap_base symbol.
  ; The ADDRESS of __heap_base equals the first free byte after static data.
  %hb_ptr = ptrtoint i8* @__heap_base to i64
  ; Align to 8 bytes
  %aligned = add i64 %hb_ptr, 7
  %heap_start = and i64 %aligned, -8
  store i64 %heap_start, i64* @__heap_ptr
  call i64 @__saffron_entry()
  ret void
}
