target triple = "wasm32-unknown-unknown"
target datalayout = "e-m:e-p:32:32-i64:64-n32:64-S128"

; =============================================================================
; WASM32 Runtime Base
; Pointers are 32-bit, all Saffron values are i64.
; The codegen emits ALL function parameters/returns as i64.
; ptrtoint/inttoptr zero-extend/truncate between i32 pointers and i64 values.
;
; malloc is defined as (i64)->i8* because the codegen emits i64 sizes.
; Internally we truncate to i32 for the actual bump allocation.
; =============================================================================

; --- Globals ---

; __heap_base is a linker-provided symbol whose ADDRESS equals the first byte
; after all static data.  We use ptrtoint on it (not a load) to get the value.
@__heap_base = external global i8
@__heap_ptr = global i32 0
@__argc = weak global i32 0
@__argv = weak global i8** null
@__exception_value = weak global i64 0
@__jmp_buf_stack = weak global [64 x i8] zeroinitializer
@__jmp_buf_current = weak global i8* null

; Runtime global: SLOT_SIZE is always 8 (sizeof i64).
@__g_SLOT_SIZE = global i64 8

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
declare void @js_dom_set_attr(i64, i8*, i8*)
declare void @js_dom_append_child(i64, i64)
declare void @js_dom_remove_child(i64, i64)
declare void @js_dom_set_inner_html(i64, i8*)
declare i64 @js_dom_query_selector(i8*)
declare void @js_dom_add_event_listener(i64, i8*, i64)
declare void @js_dom_add_event(i64, i8*, i64)
declare void @js_dom_add_typed_event_listener(i64, i8*, i64)
declare void @js_dom_insert_before(i64, i64, i64)
declare void @js_dom_set_property(i64, i8*, i8*)
declare void @js_dom_set_bool_property(i64, i8*, i64)

; --- Event Operations (provided by JS glue) ---

declare i64 @js_event_get_float(i64, i8*)
declare i64 @js_event_get_string(i64, i8*)
declare i64 @js_event_get_bool(i64, i8*)
declare void @js_event_prevent_default(i64)
declare void @js_event_stop_propagation(i64)

; --- Turmeric Framework (provided by JS glue) ---

declare i64 @turmeric_signal_subscribe(i64)
declare i64 @turmeric_reconcile_each(i64)

; --- Memory Allocator (bump allocator) ---
; Simple and fast. No free. Suitable for short-lived WASM modules.
; Accepts i64 size (what the codegen emits), truncates to i32 internally.

define i8* @malloc(i64 %size) {
entry:
  %size32 = trunc i64 %size to i32
  ; Align to 8 bytes
  %aligned = add i32 %size32, 7
  %mask = and i32 %aligned, -8
  ; Load current heap pointer (i32)
  %heap = load i32, i32* @__heap_ptr
  ; Bump heap pointer
  %new_heap = add i32 %heap, %mask
  ; Grow memory if needed: check if new_heap exceeds current memory size
  %mem_pages = call i32 @llvm.wasm.memory.size.i32(i32 0)
  %mem_bytes = mul i32 %mem_pages, 65536
  %needs_grow = icmp ugt i32 %new_heap, %mem_bytes
  br i1 %needs_grow, label %grow, label %done
grow:
  ; Calculate pages needed: (new_heap - mem_bytes + 65535) / 65536
  %deficit = sub i32 %new_heap, %mem_bytes
  %deficit_plus = add i32 %deficit, 65535
  %pages_needed = udiv i32 %deficit_plus, 65536
  %grow_result = call i32 @llvm.wasm.memory.grow.i32(i32 0, i32 %pages_needed)
  br label %done
done:
  store i32 %new_heap, i32* @__heap_ptr
  %ptr = inttoptr i32 %heap to i8*
  ret i8* %ptr
}

declare i32 @llvm.wasm.memory.size.i32(i32)
declare i32 @llvm.wasm.memory.grow.i32(i32, i32)

define i8* @calloc(i64 %num, i64 %size) {
entry:
  %total = mul i64 %num, %size
  %ptr = call i8* @malloc(i64 %total)
  ; Zero the memory
  %total32 = trunc i64 %total to i32
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%next, %body]
  %done = icmp uge i32 %i, %total32
  br i1 %done, label %end, label %body
body:
  %i64 = zext i32 %i to i64
  %p = getelementptr i8, i8* %ptr, i64 %i64
  store i8 0, i8* %p
  %next = add i32 %i, 1
  br label %loop
end:
  ret i8* %ptr
}

define i8* @realloc(i8* %old_ptr, i64 %new_size) {
entry:
  ; Bump allocator: just allocate new block and copy
  %new_ptr = call i8* @malloc(i64 %new_size)
  ; Copy old data (conservatively copy new_size bytes)
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

define i8* @strdup(i8* %s) {
entry:
  %len = call i64 @strlen(i8* %s)
  %size = add i64 %len, 1
  %buf = call i8* @malloc(i64 %size)
  call i8* @strcpy(i8* %buf, i8* %s)
  ret i8* %buf
}

define i8* @memcpy(i8* %dst, i8* %src, i64 %n) {
entry:
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %dst, i8* %src, i64 %n, i1 false)
  ret i8* %dst
}

declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)

; --- String Equality ---
; __string_eq: Fast string equality check.
;   1. Pointer equality (O(1) — same literal or same interned string)
;   2. Falls back to strcmp for dynamically-created strings
; Returns 1 (equal) or 0 (not equal) as i64.

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

; --- I/O Dispatch ---

define void @__io_println_str(i64 %s) {
entry:
  %ptr = call i8* @__val_untag_ptr(i64 %s)
  call void @js_log_str(i8* %ptr)
  ret void
}

define void @__io_println_int(i64 %n) {
entry:
  %raw = call i64 @__val_untag_int(i64 %n)
  call void @js_log_int(i64 %raw)
  ret void
}

define void @__io_println_bool(i64 %b) {
entry:
  %raw = call i64 @__val_untag_bool(i64 %b)
  call void @js_log_bool(i64 %raw)
  ret void
}

define void @__io_println_nil() {
entry:
  call void @js_log_nil()
  ret void
}

define void @__io_print_str(i64 %s) {
entry:
  %ptr = call i8* @__val_untag_ptr(i64 %s)
  call void @js_log_str(i8* %ptr)
  ret void
}

define void @__io_print_int(i64 %n) {
entry:
  %raw = call i64 @__val_untag_int(i64 %n)
  call void @js_log_int(i64 %raw)
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
  ; Minimal stub -- just logs the format string
  call void @js_log_str(i8* %fmt)
  ret i32 0
}

; --- snprintf stub ---

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

; --- strtod stub ---

define double @strtod(i8* %s, i8* %endptr) {
entry:
  ; Minimal stub: convert integer portion only
  %ival = call i64 @atol(i8* %s)
  %fval = sitofp i64 %ival to double
  ret double %fval
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

define i64 @write(i32 %fd, i8* %buf, i64 %len) {
entry:
  call void @js_log_str(i8* %buf)
  ret i64 %len
}

define i32 @access(i8* %path, i32 %mode) {
entry:
  ret i32 -1
}

define i32 @mkdir(i8* %path, i32 %mode) {
entry:
  ret i32 -1
}

define i8* @getcwd(i8* %buf, i64 %size) {
entry:
  ret i8* null
}

define i8* @getenv(i8* %name) {
entry:
  ret i8* null
}

; --- Exception handling stubs ---

define i32 @setjmp(i8* %buf) {
entry:
  ret i32 0
}

define void @longjmp(i8* %buf, i32 %val) {
entry:
  ; In WASM we cannot truly longjmp. Return and hope for the best.
  ; A proper implementation would need Emscripten-style setjmp/longjmp emulation.
  ret void
}

; --- exit ---

declare void @__builtin_trap()

define void @exit(i32 %code) {
entry:
  call void @__builtin_trap()
  unreachable
}

; =============================================================================
; NaN-Boxing Value Helpers
; =============================================================================
;
; Encoding (matches base_nanbox.ll):
;   Valid double (not NaN)         -> Float (stored directly, zero cost)
;   0x7FF8_xxxx_xxxx_xxxx         -> Heap pointer (48-bit address in payload)
;   0x7FF9_xxxx_xxxx_xxxx         -> Integer (48-bit signed in payload)
;   0x7FFA_0000_0000_0001         -> true
;   0x7FFA_0000_0000_0000         -> false
;   0x7FFA_0000_0000_0002         -> nil
;
; Constants:
;   TAG_PTR      = 0x7FF8000000000000 = 9221120237041090560
;   TAG_INT      = 0x7FF9000000000000 = 9221401712017801216
;   TAG_SPEC     = 0x7FFA000000000000 = 9221683186994511872
;   PAYLOAD_MASK = 0x0000FFFFFFFFFFFF = 281474976710655
;   VAL_TRUE     = 0x7FFA000000000001 = 9221683186994511873
;   VAL_FALSE    = 0x7FFA000000000000 = 9221683186994511872
;   VAL_NIL      = 0x7FFA000000000002 = 9221683186994511874

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
  ; wasm32 pointers are 32-bit; ptrtoint zero-extends to i64
  %int_ptr = ptrtoint i8* %ptr to i64
  %masked = and i64 %int_ptr, 281474976710655
  %tagged = or i64 %masked, 9221120237041090560
  ret i64 %tagged
}

define i8* @__val_untag_ptr(i64 %v) {
entry:
  ; Mask off the tag bits to get the raw pointer value
  %ptr_int = and i64 %v, 281474976710655
  ; On wasm32, inttoptr truncates i64 to i32 pointer
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
  ; A value is a float if it's NOT in our NaN-tagged range
  ; Check that the upper 16 bits are NOT 0x7FF8, 0x7FF9, or 0x7FFA
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
  %result = icmp eq i64 %v, 9221683186994511874
  ret i1 %result
}

define i1 @__val_is_true(i64 %v) {
entry:
  ; true = 0x7FFA000000000001
  %result = icmp eq i64 %v, 9221683186994511873
  ret i1 %result
}

define i1 @__val_is_bool(i64 %v) {
entry:
  %is_t = icmp eq i64 %v, 9221683186994511873  ; true  (0x7FFA000000000001)
  %is_f = icmp eq i64 %v, 9221683186994511872  ; false (0x7FFA000000000000)
  %result = or i1 %is_t, %is_f
  ret i1 %result
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

; --- Heap Object Type ID ---

define i64 @__val_type_id(i64 %v) {
entry:
  ; For a heap pointer, read the type ID from the first field of the object
  %ptr_int = and i64 %v, 281474976710655      ; mask off tag
  %ptr = inttoptr i64 %ptr_int to i64*
  %type_id = load i64, i64* %ptr
  ret i64 %type_id
}

; =============================================================================
; to_string Helpers
; =============================================================================

@.str.true = private unnamed_addr constant [5 x i8] c"true\00"
@.str.false = private unnamed_addr constant [6 x i8] c"false\00"
@.str.nil = private unnamed_addr constant [4 x i8] c"nil\00"

define i64 @__bool_to_string(i64 %b) {
entry:
  %raw = call i64 @__val_untag_bool(i64 %b)
  %is_true = icmp ne i64 %raw, 0
  br i1 %is_true, label %yes, label %no
yes:
  %t = getelementptr [5 x i8], [5 x i8]* @.str.true, i64 0, i64 0
  %r1 = ptrtoint i8* %t to i64
  ret i64 %r1
no:
  %f = getelementptr [6 x i8], [6 x i8]* @.str.false, i64 0, i64 0
  %r2 = ptrtoint i8* %f to i64
  ret i64 %r2
}

define i64 @__nil_to_string() {
entry:
  %s = getelementptr [4 x i8], [4 x i8]* @.str.nil, i64 0, i64 0
  %r = ptrtoint i8* %s to i64
  ret i64 %r
}

define i64 @__float_to_string(i64 %v) {
entry:
  %f = bitcast i64 %v to double
  %ival = fptosi double %f to i64
  %result = call i64 @__int_to_string(i64 %ival)
  ret i64 %result
}

; Wrapper: tag a raw pointer as a Saffron string value (i64 -> i64)
define i64 @__rt_tag_ptr(i64 %raw) {
entry:
  %ptr = inttoptr i64 %raw to i8*
  %tagged = call i64 @__val_tag_ptr(i8* %ptr)
  ret i64 %tagged
}

; =============================================================================
; GC Stubs (no actual garbage collection in WASM -- bump allocator is sufficient)
; =============================================================================

define void @__gc_enable() {
entry:
  ret void
}

define void @__gc_disable() {
entry:
  ret void
}

define void @__gc_collect() {
entry:
  ret void
}

define void @__gc_set_threshold(i64 %bytes) {
entry:
  ret void
}

define void @__gc_push_root(i64 %root_addr) {
entry:
  ret void
}

define void @__gc_pop_roots(i64 %n) {
entry:
  ret void
}

define i64 @__gc_shadow_stack_depth() {
entry:
  ret i64 0
}

define void @__gc_init_shadow_stack() {
entry:
  ret void
}

define void @__gc_debug_stats() {
entry:
  ret void
}

; __gc_alloc: just malloc (no header, no tracking)
; Both size and type_tag are i64 (what the codegen emits)
define i64 @__gc_alloc(i64 %size, i64 %type_tag) {
entry:
  %ptr = call i8* @malloc(i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_alloc_zeroed: calloc equivalent
define i64 @__gc_alloc_zeroed(i64 %size, i64 %type_tag) {
entry:
  %ptr = call i8* @calloc(i64 1, i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_realloc: simple realloc
define i64 @__gc_realloc(i64 %old_ptr, i64 %new_size, i64 %type_tag) {
entry:
  %old_p = inttoptr i64 %old_ptr to i8*
  %new_p = call i8* @realloc(i8* %old_p, i64 %new_size)
  %r = ptrtoint i8* %new_p to i64
  ret i64 %r
}

; __gc_list_new: allocate a list { count@0, capacity@8, data_ptr@16 }
define i64 @__gc_list_new() {
entry:
  %list_raw = call i8* @calloc(i64 1, i64 24)
  %list = ptrtoint i8* %list_raw to i64
  ; capacity = 8
  %cap_addr = add i64 %list, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 8, i64* %cap_ptr
  ; data = calloc(64) for 8 slots
  %data_raw = call i8* @calloc(i64 1, i64 64)
  %data = ptrtoint i8* %data_raw to i64
  %data_addr = add i64 %list, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  store i64 %data, i64* %data_ptr
  ret i64 %list
}

; __gc_list_push: push a value onto a list
define void @__gc_list_push(i64 %list, i64 %value) {
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

; __gc_map_new: allocate a map { count@0, capacity@8, keys_ptr@16, values_ptr@24 }
define i64 @__gc_map_new() {
entry:
  %map_raw = call i8* @calloc(i64 1, i64 32)
  %map = ptrtoint i8* %map_raw to i64
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
  ret i64 %map
}

; __gc_stringbuilder_new: allocate a StringBuilder { len@0, cap@8, buf_ptr@16 }
define i64 @__gc_stringbuilder_new() {
entry:
  %sb_raw = call i8* @calloc(i64 1, i64 24)
  %sb = ptrtoint i8* %sb_raw to i64
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
  ret i64 %sb
}

; __gc_alloc_safe: allocate without triggering GC (wasm32: just malloc)
define i64 @__gc_alloc_safe(i64 %size, i64 %type_tag) {
entry:
  %ptr = call i8* @malloc(i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_string_alloc: allocate a string buffer
define i64 @__gc_string_alloc(i64 %size) {
entry:
  %ptr = call i8* @malloc(i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_closure_new: allocate a closure pair { fn_ptr@0, env_ptr@8 }
define i64 @__gc_closure_new(i64 %fn_ptr, i64 %env_ptr) {
entry:
  %raw_p = call i8* @malloc(i64 16)
  %raw = ptrtoint i8* %raw_p to i64
  %fn_slot = inttoptr i64 %raw to i64*
  store i64 %fn_ptr, i64* %fn_slot
  %env_addr = add i64 %raw, 8
  %env_slot = inttoptr i64 %env_addr to i64*
  store i64 %env_ptr, i64* %env_slot
  ret i64 %raw
}

; __gc_env_alloc: allocate a closure environment (zeroed)
define i64 @__gc_env_alloc(i64 %num_slots) {
entry:
  %size = shl i64 %num_slots, 3
  %ptr = call i8* @calloc(i64 1, i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; __gc_instance_alloc: allocate a class instance (zeroed)
define i64 @__gc_instance_alloc(i64 %num_fields) {
entry:
  %size = shl i64 %num_fields, 3
  %ptr = call i8* @calloc(i64 1, i64 %size)
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}

; GC statistics: all return 0
define i64 @__gc_stat_alloc_count() {
entry:
  ret i64 0
}

define i64 @__gc_stat_total_bytes() {
entry:
  ret i64 0
}

define i64 @__gc_stat_collections() {
entry:
  ret i64 0
}

define i64 @__gc_stat_freed_bytes() {
entry:
  ret i64 0
}

define i64 @__gc_stat_threshold() {
entry:
  ret i64 0
}

; =============================================================================
; __any_to_string -- Runtime type dispatch for NaN-boxed values
; =============================================================================
; Takes a NaN-boxed i64, determines its type at runtime, and returns a raw
; char* (as i64) pointing to the string representation.

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
  ; Untag the pointer -- caller expects a raw i8*
  %raw_ptr = call i8* @__val_untag_ptr(i64 %val)
  %ptr_as_i64 = ptrtoint i8* %raw_ptr to i64
  ret i64 %ptr_as_i64

do_float:
  ; Must be a float (not NaN-tagged)
  %float_str = call i64 @__float_to_string(i64 %val)
  ret i64 %float_str

ret_nil:
  %nil_str = getelementptr [4 x i8], [4 x i8]* @.str.nil, i64 0, i64 0
  %nil_ptr = ptrtoint i8* %nil_str to i64
  ret i64 %nil_ptr

ret_true:
  %true_str = getelementptr [5 x i8], [5 x i8]* @.str.true, i64 0, i64 0
  %true_ptr = ptrtoint i8* %true_str to i64
  ret i64 %true_ptr

ret_false:
  %false_str = getelementptr [6 x i8], [6 x i8]* @.str.false, i64 0, i64 0
  %false_ptr = ptrtoint i8* %false_str to i64
  ret i64 %false_ptr
}

; __io_println_any -- Print any NaN-boxed value followed by a newline.
define void @__io_println_any(i64 %val) {
entry:
  %str = call i64 @__any_to_string(i64 %val)
  %ptr = inttoptr i64 %str to i8*
  call void @js_log_str(i8* %ptr)
  ret void
}

; __io_println -- Universal println: delegates to __io_println_any
define i64 @__io_println(i64 %val) {
entry:
  call void @__io_println_any(i64 %val)
  ret i64 0
}

; __io_print -- Universal print (no newline): converts via __any_to_string
define i64 @__io_print(i64 %val) {
entry:
  %str = call i64 @__any_to_string(i64 %val)
  %ptr = inttoptr i64 %str to i8*
  call void @js_log_str(i8* %ptr)
  ret i64 0
}

; =============================================================================
; Entry point wrapper
; WASM entry point -- initializes heap, then calls __saffron_entry.
; =============================================================================

declare i64 @__saffron_entry()

define void @_start() {
entry:
  ; Initialize heap pointer from linker-provided __heap_base symbol.
  ; On wasm32, ptrtoint gives a 32-bit value.
  %hb_ptr = ptrtoint i8* @__heap_base to i32
  ; Align to 8 bytes
  %aligned = add i32 %hb_ptr, 7
  %heap_start = and i32 %aligned, -8
  store i32 %heap_start, i32* @__heap_ptr
  call i64 @__saffron_entry()
  ret void
}
