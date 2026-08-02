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

; --- Cooperative scheduler state ---
; The native build gets these from base_nanbox.ll plus async_native.c. WASM has
; no async_native.c (it is compiled for the host), so the equivalent lives here
; in IR. Nothing in the scheduler needs threads: LLVM coroutines are an IR-level
; transformation (CoroSplit turns each coroutine into a state machine with a
; heap-allocated frame before instruction selection), so they lower to wasm32
; unchanged. What was missing was only this plumbing.
@__yield_reason = global i64 0
@__yield_arg = global i64 0
@__task_result = global i64 0

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

; --- Memory cap shims ---
;
; runtime.sf binds rt_malloc/rt_realloc/rt_free to __sf_malloc/__sf_realloc/
; __sf_free so that --max-memory covers the native runtime. Those wrappers live
; in gc.ll, which is not linked for wasm, so provide pass-throughs here purely
; for link compatibility.
;
; The cap itself is NOT enforced on wasm and is out of scope: this bump
; allocator discards memory.grow's result (see @malloc above), so it cannot even
; detect an out-of-memory condition today. Capping wasm needs that fixed first.

define i8* @__sf_malloc(i64 %size) {
entry:
  %p = call i8* @malloc(i64 %size)
  ret i8* %p
}

define i8* @__sf_realloc(i8* %old, i64 %size) {
entry:
  %p = call i8* @realloc(i8* %old, i64 %size)
  ret i8* %p
}

define void @__sf_free(i8* %p) {
entry:
  ret void
}

define void @__mem_set_limit(i64 %bytes) {
entry:
  ret void
}

define i64 @__mem_get_limit() {
entry:
  ret i64 0
}

define i64 @__mem_live_bytes() {
entry:
  ret i64 0
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
; __builtin_trap is defined further down (it lowers to wasm's own trap
; instruction and needs no host import).

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

; @generated-values:begin -- DO NOT EDIT BELOW THIS LINE
; Generated from src/runtime/values.spec for target `wasm32` (discipline:
; nanbox) by tools/gen_runtime_values.py. Edit the spec, then re-run:
;
;     python3 tools/gen_runtime_values.py
;
; These 19 helpers are shared across four IR bases. They were hand-copied and
; drifted -- BUGS #77 had `true` printing as "false" on wasm32 for months because
; one base out of four untagged twice. Editing this block directly reintroduces
; exactly that failure mode, and `--check` in CI will fail.

; --- Tag/Untag Helpers ---

define i64 @__val_tag_int(i64 %n) {
entry:
  %masked = and i64 %n, 281474976710655
  %tagged = or i64 %masked, 9221401712017801216
  ret i64 %tagged
}

define i64 @__val_untag_int(i64 %v) {
entry:
  ; Check if value is NaN-boxed (top 16 bits in 0x7FF8..0x7FFF)
  %tag_bits = lshr i64 %v, 48
  %ge_min = icmp uge i64 %tag_bits, 32760
  %le_max = icmp ule i64 %tag_bits, 32767
  %is_tagged = and i1 %ge_min, %le_max
  br i1 %is_tagged, label %extract_int, label %check_raw
check_raw:
  ; Not NaN-boxed. Could be:
  ; 1) A raw small integer passed directly (0, 1, 2, ...) from codegen
  ; 2) A float-tagged double from arithmetic (e.g., 1.0 = 0x3FF0000000000000)
  ; Distinguish: raw ints are small (< 2^31), float bits for numbers >= 1.0 are >= 0x3FF0...
  ; Any value fitting in 32 bits is a raw integer (wasm32 addresses/indices fit in 32 bits)
  %high_bits = lshr i64 %v, 32
  %is_small = icmp eq i64 %high_bits, 0
  br i1 %is_small, label %raw_int, label %from_float
raw_int:
  ; Value fits in 32 bits — it's a raw integer, return as-is
  ret i64 %v
extract_int:
  ; NaN-boxed value: extract lower 48-bit payload as signed integer
  %payload = and i64 %v, 281474976710655
  %shift_left = shl i64 %payload, 16
  %sign_ext = ashr i64 %shift_left, 16
  ret i64 %sign_ext
from_float:
  ; Value has bits in upper 32 — it's a float from arithmetic. Convert to int.
  ; Guard against NaN/Inf (exponent field all 1s)
  %exp_bits = lshr i64 %v, 52
  %exp_masked = and i64 %exp_bits, 2047
  %is_special = icmp eq i64 %exp_masked, 2047
  br i1 %is_special, label %ret_zero, label %safe_convert
safe_convert:
  %f = bitcast i64 %v to double
  %as_int = fptosi double %f to i64
  ret i64 %as_int
ret_zero:
  ret i64 0
}

define i64 @__val_tag_ptr(i8* %ptr) {
entry:
  ; ptrtoint widens a 32-bit pointer by zero-extension on wasm32; on a 64-bit
  ; host the mask below is what keeps the tag bits clear.
  %int_ptr = ptrtoint i8* %ptr to i64
  %masked = and i64 %int_ptr, 281474976710655
  %tagged = or i64 %masked, 9221120237041090560
  ret i64 %tagged
}

define i8* @__val_untag_ptr(i64 %v) {
entry:
  ; Mask off the tag bits to get the raw pointer value. inttoptr is a no-op on a
  ; 64-bit host and truncates to a 32-bit pointer on wasm32.
  %ptr_int = and i64 %v, 281474976710655
  %ptr = inttoptr i64 %ptr_int to i8*
  ret i8* %ptr
}

define i64 @__val_tag_float(double %f) {
entry:
  %bits = bitcast double %f to i64
  ; A positive quiet NaN can be bit-identical to one of our tags: 0x7FF8 is
  ; TAG_PTR with a null payload, 0x7FF9 is TAG_INT, 0x7FFA is TAG_SPEC. Those
  ; are the only doubles that can reach the tag range at all (it needs an
  ; all-ones exponent plus a set high mantissa bit), so remapping them to a
  ; quiet NaN just outside the range leaves every finite double untouched while
  ; making `0.0/0.0` distinguishable from a null pointer. Without this, NaN was
  ; read back as a tagged null pointer and dereferenced.
  %upper = lshr i64 %bits, 48
  %ge_tag = icmp uge i64 %upper, 32760          ; 0x7FF8
  %le_tag = icmp ule i64 %upper, 32762          ; 0x7FFA
  %collides = and i1 %ge_tag, %le_tag
  ; 0x7FFC000000000000 — still a quiet NaN, outside 0x7FF8..0x7FFA.
  %safe = select i1 %collides, i64 9222246136947933184, i64 %bits
  ret i64 %safe
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

; @generated-values:end

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

; __float_to_string -- format a double as decimal text.
;
; This used to be `fptosi` + __int_to_string, i.e. it simply TRUNCATED. Every
; float printed on a wasm target lost its fractional part: 12.5664 came out as
; "12" and 3.5 as "3", silently and everywhere -- to_string(), string
; interpolation, and __any_to_string alike. The native targets format with
; snprintf("%g"), but snprintf is only a stub here (see above), so the digits
; have to be produced by hand.
;
; Integer part via the existing __wasm_uint_to_str, then up to 6 fractional
; digits. The fraction is scaled to an integer ONCE and the digits extracted
; with exact integer division; generating them one at a time by repeated
; multiply-and-truncate accumulates binary representation error and turns
; 12.5664 into "12.566399". Trailing zeros are trimmed, so a whole number still
; prints as "3" rather than "3.000000".
;
; Divergence from %g worth knowing: this is 6 decimal PLACES, not 6 significant
; digits, and there is no exponent form. Very small magnitudes therefore print
; as "0.000012" where the native runtime would say "1.2345e-05", and values
; beyond i64 range are not representable at all.
define i64 @__float_to_string(i64 %v) {
entry:
  %f0 = bitcast i64 %v to double
  %buf = call i8* @malloc(i64 48)
  %isneg = fcmp olt double %f0, 0.000000e+00
  br i1 %isneg, label %do_neg, label %after_sign

do_neg:
  store i8 45, i8* %buf
  %bufn = getelementptr i8, i8* %buf, i64 1
  %fneg = fsub double 0.000000e+00, %f0
  br label %after_sign

after_sign:
  %cur = phi i8* [ %buf, %entry ], [ %bufn, %do_neg ]
  %f = phi double [ %f0, %entry ], [ %fneg, %do_neg ]
  %ipart0 = fptosi double %f to i64
  %ipd0 = sitofp i64 %ipart0 to double
  %frac = fsub double %f, %ipd0
  ; scaled = round(frac * 1e6), as exact integer math from here on
  %scaled_f = fmul double %frac, 1.000000e+06
  %scaled_r = fadd double %scaled_f, 5.000000e-01
  %scaled0 = fptosi double %scaled_r to i64
  ; frac ~= 0.9999995 rounds up to a full unit: carry into the integer part
  %carry = icmp sge i64 %scaled0, 1000000
  %ipart_inc = add i64 %ipart0, 1
  %ipart = select i1 %carry, i64 %ipart_inc, i64 %ipart0
  %scaled = select i1 %carry, i64 0, i64 %scaled0
  call void @__wasm_uint_to_str(i64 %ipart, i8* %cur)
  %ilen = call i64 @strlen(i8* %cur)
  %fracpos = getelementptr i8, i8* %cur, i64 %ilen
  %no_frac = icmp eq i64 %scaled, 0
  br i1 %no_frac, label %finish_int, label %digits

digits:
  ; Fill tmp[0..5] most-significant first by peeling off the low digit and
  ; walking backwards.
  %tmp = alloca [8 x i8]
  %tmpp = getelementptr [8 x i8], [8 x i8]* %tmp, i64 0, i64 0
  br label %dloop

dloop:
  %di = phi i64 [ 6, %digits ], [ %dinext, %dloop ]
  %sv = phi i64 [ %scaled, %digits ], [ %svnext, %dloop ]
  %dinext = sub i64 %di, 1
  %rem = urem i64 %sv, 10
  %svnext = udiv i64 %sv, 10
  %dgc = add i64 %rem, 48
  %dgc8 = trunc i64 %dgc to i8
  %dslot = getelementptr i8, i8* %tmpp, i64 %dinext
  store i8 %dgc8, i8* %dslot
  %ddone = icmp eq i64 %dinext, 0
  br i1 %ddone, label %trim, label %dloop

trim:
  br label %tloop

tloop:
  ; Walk back over trailing '0's. %scaled is nonzero here, so at least one
  ; significant digit survives and this cannot run off the front.
  %tn = phi i64 [ 6, %trim ], [ %tnnext, %tcont ]
  %lastidx = sub i64 %tn, 1
  %lastp = getelementptr i8, i8* %tmpp, i64 %lastidx
  %lastc = load i8, i8* %lastp
  %iszero = icmp eq i8 %lastc, 48
  br i1 %iszero, label %tcont, label %write_frac

tcont:
  %tnnext = sub i64 %tn, 1
  br label %tloop

write_frac:
  store i8 46, i8* %fracpos
  %fstart = getelementptr i8, i8* %fracpos, i64 1
  br label %wloop

wloop:
  %wi = phi i64 [ 0, %write_frac ], [ %winext, %wloop ]
  %srcp = getelementptr i8, i8* %tmpp, i64 %wi
  %sc = load i8, i8* %srcp
  %dstp = getelementptr i8, i8* %fstart, i64 %wi
  store i8 %sc, i8* %dstp
  %winext = add i64 %wi, 1
  %wdone = icmp uge i64 %winext, %tn
  br i1 %wdone, label %wend, label %wloop

wend:
  %endp = getelementptr i8, i8* %fstart, i64 %tn
  store i8 0, i8* %endp
  %r1 = ptrtoint i8* %buf to i64
  ret i64 %r1

finish_int:
  ; __wasm_uint_to_str already NUL-terminated; nothing fractional to add.
  %r0 = ptrtoint i8* %buf to i64
  ret i64 %r0
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

; =============================================================================
; GC header layout (wasm32)
; =============================================================================
;
; Layout: [type_tag: i64][magic: i64][user data...]
;         ^                          ^-- returned pointer
;         ptr - 16                   ptr - 8 holds the magic
;
; The header is 16 bytes, not the 8 it used to be, so that the magic sentinel
; lands at ptr - 8 -- the same offset native uses. `runtime.sf` hardcodes that
; offset in __rt_as_list_ptr / __rt_as_map_ptr / __rt_as_string_ptr, which read
; load64(raw - 8) and compare against the sentinel before trusting the type tag.
; With the old 8-byte header those helpers read the *type tag* instead (a small
; integer, never equal to the sentinel), so every collection was rejected and
; IO.println fell through to the float path -- BUGS #39.
;
; Native's header is 24 bytes ([next][info][magic]) because it threads a real
; collector: a linked list and a packed size/tag word. wasm32 never collects, so
; it needs neither; only the magic's *position* has to agree. Keeping the tag as
; a plain word (rather than native's packed `info`) is why __gc_get_type_tag
; below reads it directly instead of calling __gc_info_tag.
;
; 0x5AFFC0DEDEADBEEF = 6557403441622859503

define i64 @__gc_alloc(i64 %size, i64 %type_tag) {
entry:
  %total = add i64 %size, 16
  %raw = call i8* @malloc(i64 %total)
  %raw_int = ptrtoint i8* %raw to i64
  ; header[0] = type tag
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 %type_tag, i64* %tag_ptr
  ; header[8] = magic sentinel
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  ; user data starts after the 16-byte header
  %user = add i64 %raw_int, 16
  ret i64 %user
}

; __gc_alloc_zeroed: calloc with the 16-byte header
define i64 @__gc_alloc_zeroed(i64 %size, i64 %type_tag) {
entry:
  %total = add i64 %size, 16
  %raw = call i8* @calloc(i64 1, i64 %total)
  %raw_int = ptrtoint i8* %raw to i64
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 %type_tag, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  %user = add i64 %raw_int, 16
  ret i64 %user
}

; __gc_realloc: realloc preserving the header
define i64 @__gc_realloc(i64 %old_ptr, i64 %new_size, i64 %type_tag) {
entry:
  ; old_ptr points to user data; the real allocation is at old_ptr - 16
  %old_raw = sub i64 %old_ptr, 16
  %old_raw_p = inttoptr i64 %old_raw to i8*
  %new_total = add i64 %new_size, 16
  %new_raw_p = call i8* @realloc(i8* %old_raw_p, i64 %new_total)
  %new_raw = ptrtoint i8* %new_raw_p to i64
  ; Re-store the header (realloc may have moved the block)
  %tag_ptr = inttoptr i64 %new_raw to i64*
  store i64 %type_tag, i64* %tag_ptr
  %magic_addr = add i64 %new_raw, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  %user = add i64 %new_raw, 16
  ret i64 %user
}

; __gc_get_type_tag: read the type tag stored at ptr - 16
define i64 @__gc_get_type_tag(i64 %ptr) {
entry:
  %is_null = icmp eq i64 %ptr, 0
  br i1 %is_null, label %ret_zero, label %read_tag
read_tag:
  %tag_addr = sub i64 %ptr, 16
  %tag_ptr = inttoptr i64 %tag_addr to i64*
  %tag = load i64, i64* %tag_ptr
  ret i64 %tag
ret_zero:
  ret i64 0
}

; __gc_list_new: allocate a list { count@0, capacity@8, data_ptr@16 }
; Stores type tag 2 and the magic in the 16-byte header before the returned
; pointer. The data buffer carries the same header (type 7 = data array).
define i64 @__gc_list_new() {
entry:
  ; Allocate 16 (header) + 24 (list struct) = 40 bytes
  %raw = call i8* @calloc(i64 1, i64 40)
  %raw_int = ptrtoint i8* %raw to i64
  ; Store type tag = 2 (list) at header[0]
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 2, i64* %tag_ptr
  ; Store the magic at header[8]
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  ; User pointer starts after the header
  %list = add i64 %raw_int, 16
  ; capacity = 8  (struct field, not a header offset)
  %cap_addr = add i64 %list, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 8, i64* %cap_ptr
  ; data = calloc(16 + 64) for 8 slots, with its own header
  %data_raw = call i8* @calloc(i64 1, i64 80)
  %data_raw_int = ptrtoint i8* %data_raw to i64
  ; Store data type tag = 7 (data array)
  %data_tag_ptr = inttoptr i64 %data_raw_int to i64*
  store i64 7, i64* %data_tag_ptr
  %data_magic_addr = add i64 %data_raw_int, 8
  %data_magic_ptr = inttoptr i64 %data_magic_addr to i64*
  store i64 6557403441622859503, i64* %data_magic_ptr
  ; Data user pointer after its header
  %data = add i64 %data_raw_int, 16
  %data_addr = add i64 %list, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  store i64 %data, i64* %data_ptr
  ret i64 %list
}

; __gc_list_push: push a value onto a list
; The data buffer has a 16-byte header, so realloc adjusts for it.
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
  ; Data has a 16-byte header: raw allocation is at old_data - 16
  %data_addr_g = add i64 %list, 16
  %data_ptr_g = inttoptr i64 %data_addr_g to i64*
  %old_data = load i64, i64* %data_ptr_g
  %old_raw = sub i64 %old_data, 16
  %old_raw_p = inttoptr i64 %old_raw to i8*
  %new_total = add i64 %new_bytes, 16
  %new_raw_p = call i8* @realloc(i8* %old_raw_p, i64 %new_total)
  %new_raw = ptrtoint i8* %new_raw_p to i64
  ; Re-store the header (realloc may move)
  %new_tag_ptr = inttoptr i64 %new_raw to i64*
  store i64 7, i64* %new_tag_ptr
  %new_magic_addr = add i64 %new_raw, 8
  %new_magic_ptr = inttoptr i64 %new_magic_addr to i64*
  store i64 6557403441622859503, i64* %new_magic_ptr
  %new_data = add i64 %new_raw, 16
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
; Stores type tag 3 and the magic in the 16-byte header before the returned
; pointer. Key/value buffers carry the same header (type 8 = key/value array).
define i64 @__gc_map_new() {
entry:
  ; Allocate 16 (header) + 32 (map struct) = 48 bytes
  %raw = call i8* @calloc(i64 1, i64 48)
  %raw_int = ptrtoint i8* %raw to i64
  ; Store type tag = 3 (map) at header[0]
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 3, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  ; User pointer starts after the header
  %map = add i64 %raw_int, 16
  ; capacity = 16  (struct field, not a header offset)
  %cap_addr = add i64 %map, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 16, i64* %cap_ptr
  ; keys = calloc(16 + 128) with its own header
  %keys_raw = call i8* @calloc(i64 1, i64 144)
  %keys_raw_int = ptrtoint i8* %keys_raw to i64
  %keys_tag_ptr = inttoptr i64 %keys_raw_int to i64*
  store i64 8, i64* %keys_tag_ptr
  %keys_magic_addr = add i64 %keys_raw_int, 8
  %keys_magic_ptr = inttoptr i64 %keys_magic_addr to i64*
  store i64 6557403441622859503, i64* %keys_magic_ptr
  %keys = add i64 %keys_raw_int, 16
  %keys_addr = add i64 %map, 16
  %keys_ptr = inttoptr i64 %keys_addr to i64*
  store i64 %keys, i64* %keys_ptr
  ; values = calloc(16 + 128) with its own header
  %vals_raw = call i8* @calloc(i64 1, i64 144)
  %vals_raw_int = ptrtoint i8* %vals_raw to i64
  %vals_tag_ptr = inttoptr i64 %vals_raw_int to i64*
  store i64 8, i64* %vals_tag_ptr
  %vals_magic_addr = add i64 %vals_raw_int, 8
  %vals_magic_ptr = inttoptr i64 %vals_magic_addr to i64*
  store i64 6557403441622859503, i64* %vals_magic_ptr
  %vals = add i64 %vals_raw_int, 16
  %vals_addr = add i64 %map, 24
  %vals_ptr = inttoptr i64 %vals_addr to i64*
  store i64 %vals, i64* %vals_ptr
  ret i64 %map
}

; __gc_stringbuilder_new: allocate a StringBuilder { len@0, cap@8, buf_ptr@16 }
; Stores type tag 6 and the magic in the 16-byte header before the returned
; pointer. The buffer carries the same header (type 0 = raw).
define i64 @__gc_stringbuilder_new() {
entry:
  ; Allocate 16 (header) + 24 (sb struct) = 40 bytes
  %raw = call i8* @calloc(i64 1, i64 40)
  %raw_int = ptrtoint i8* %raw to i64
  ; Store type tag = 6 (stringbuilder)
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 6, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  ; User pointer after the header
  %sb = add i64 %raw_int, 16
  ; cap = 1024  (struct field, not a header offset)
  %cap_addr = add i64 %sb, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 1024, i64* %cap_ptr
  ; buf = calloc(16 + 1024) with its own header
  %buf_raw = call i8* @calloc(i64 1, i64 1040)
  %buf_raw_int = ptrtoint i8* %buf_raw to i64
  ; Store buffer type tag = 0 (raw)
  %buf_tag_ptr = inttoptr i64 %buf_raw_int to i64*
  store i64 0, i64* %buf_tag_ptr
  %buf_magic_addr = add i64 %buf_raw_int, 8
  %buf_magic_ptr = inttoptr i64 %buf_magic_addr to i64*
  store i64 6557403441622859503, i64* %buf_magic_ptr
  ; Buffer user pointer after its header
  %buf = add i64 %buf_raw_int, 16
  %buf_addr = add i64 %sb, 16
  %buf_ptr = inttoptr i64 %buf_addr to i64*
  store i64 %buf, i64* %buf_ptr
  ret i64 %sb
}

; __gc_alloc_safe: allocate without triggering GC (wasm32: same as __gc_alloc)
define i64 @__gc_alloc_safe(i64 %size, i64 %type_tag) {
entry:
  %total = add i64 %size, 16
  %raw = call i8* @malloc(i64 %total)
  %raw_int = ptrtoint i8* %raw to i64
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 %type_tag, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  %user = add i64 %raw_int, 16
  ret i64 %user
}

; __gc_string_alloc: allocate a string buffer
; Stores type tag 1 (string) and the magic in the header.
define i64 @__gc_string_alloc(i64 %size) {
entry:
  %total = add i64 %size, 16
  %raw = call i8* @malloc(i64 %total)
  %raw_int = ptrtoint i8* %raw to i64
  ; Store type tag = 1 (string)
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 1, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  %user = add i64 %raw_int, 16
  ret i64 %user
}

; __gc_closure_new: allocate a closure pair { fn_ptr@0, env_ptr@8 }
; Stores type tag 4 and the magic in the 16-byte header.
define i64 @__gc_closure_new(i64 %fn_ptr, i64 %env_ptr) {
entry:
  ; Allocate 16 (header) + 16 (closure) = 32 bytes
  %raw_p = call i8* @malloc(i64 32)
  %raw = ptrtoint i8* %raw_p to i64
  ; Store type tag = 4 (closure)
  %tag_ptr = inttoptr i64 %raw to i64*
  store i64 4, i64* %tag_ptr
  %magic_addr = add i64 %raw, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  ; User pointer after the header
  %user = add i64 %raw, 16
  %fn_slot = inttoptr i64 %user to i64*
  store i64 %fn_ptr, i64* %fn_slot
  ; env_ptr is a struct field at user + 8, not a header offset
  %env_addr = add i64 %user, 8
  %env_slot = inttoptr i64 %env_addr to i64*
  store i64 %env_ptr, i64* %env_slot
  ret i64 %user
}

; __gc_env_alloc: allocate a closure environment (zeroed)
; Stores type tag 9 and the magic in the 16-byte header.
define i64 @__gc_env_alloc(i64 %num_slots) {
entry:
  %size = shl i64 %num_slots, 3
  %total = add i64 %size, 16
  %raw = call i8* @calloc(i64 1, i64 %total)
  %raw_int = ptrtoint i8* %raw to i64
  ; Store type tag = 9 (env)
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 9, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  %user = add i64 %raw_int, 16
  ret i64 %user
}

; __gc_instance_alloc: allocate a class instance (zeroed)
; Stores type tag 5 and the magic in the 16-byte header.
define i64 @__gc_instance_alloc(i64 %num_fields) {
entry:
  %size = shl i64 %num_fields, 3
  %total = add i64 %size, 16
  %raw = call i8* @calloc(i64 1, i64 %total)
  %raw_int = ptrtoint i8* %raw to i64
  ; Store type tag = 5 (instance)
  %tag_ptr = inttoptr i64 %raw_int to i64*
  store i64 5, i64* %tag_ptr
  %magic_addr = add i64 %raw_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  store i64 6557403441622859503, i64* %magic_ptr
  %user = add i64 %raw_int, 16
  ret i64 %user
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

; A collection value can reach here either NaN-boxed or as a bare heap pointer,
; depending on where it was produced -- __list_new and __map_new both return raw
; pointers. These accept both and return 0 for anything that is not a GC-managed
; list/map, verifying the header sentinel before trusting the type tag. Defined
; in runtime.ll, which links after this file.
declare i64 @__rt_as_list_ptr(i64)
declare i64 @__list_to_string(i64)
declare i64 @__rt_as_map_ptr(i64)
declare i64 @__map_to_string(i64)

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
  br i1 %is_int, label %do_int, label %check_list

do_int:
  %raw_int = call i64 @__val_untag_int(i64 %val)
  %int_str = call i64 @__int_to_string(i64 %raw_int)
  ret i64 %int_str

  ; Test for a list or map before the pointer/float split. An untagged
  ; collection pointer fails __val_is_ptr (its top 16 bits are 0, not 0x7FF8)
  ; and would otherwise fall through to do_float and be reinterpreted as a
  ; double, printing garbage instead of the collection. Mirrors base_nanbox.ll.
  ; This is only sound now that the wasm32 GC header carries the magic sentinel
  ; at ptr - 8, which is what __rt_as_list_ptr/__rt_as_map_ptr check (BUGS #39).
check_list:
  %as_list = call i64 @__rt_as_list_ptr(i64 %val)
  %is_list = icmp ne i64 %as_list, 0
  br i1 %is_list, label %do_list, label %check_map

do_list:
  %list_str = call i64 @__list_to_string(i64 %as_list)
  ret i64 %list_str

check_map:
  %as_map = call i64 @__rt_as_map_ptr(i64 %val)
  %is_map = icmp ne i64 %as_map, 0
  br i1 %is_map, label %do_map, label %check_ptr

do_map:
  %map_str = call i64 @__map_to_string(i64 %as_map)
  ret i64 %map_str

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
; __any_eq / __any_ne — Deep equality for Any-typed NaN-boxed values
; =============================================================================

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
  br i1 %both_ptr, label %ptr_compare, label %check_numeric

check_numeric:
  ; Check if both values are numeric (Int or Float)
  %a_is_int = icmp eq i64 %a_upper, 32761
  %a_is_spec = icmp eq i64 %a_upper, 32762
  %a_is_tagged = or i1 %a_is_ptr, %a_is_int
  %a_is_tagged2 = or i1 %a_is_tagged, %a_is_spec
  %a_is_float = xor i1 %a_is_tagged2, true
  %a_is_num = or i1 %a_is_int, %a_is_float

  %b_is_int = icmp eq i64 %b_upper, 32761
  %b_is_spec = icmp eq i64 %b_upper, 32762
  %b_is_tagged = or i1 %b_is_ptr, %b_is_int
  %b_is_tagged2 = or i1 %b_is_tagged, %b_is_spec
  %b_is_float = xor i1 %b_is_tagged2, true
  %b_is_num = or i1 %b_is_int, %b_is_float

  %both_num = and i1 %a_is_num, %b_is_num
  br i1 %both_num, label %numeric_compare, label %check_bool

check_bool:
  %a_is_bool = icmp eq i64 %a_upper, 32762
  %b_is_bool = icmp eq i64 %b_upper, 32762
  %both_bool = and i1 %a_is_bool, %b_is_bool
  br i1 %both_bool, label %bool_compare, label %not_equal

bool_compare:
  %bool_eq = icmp eq i64 %a, %b
  br i1 %bool_eq, label %equal, label %not_equal

numeric_compare:
  %a_dbl = call double @__val_untag_float(i64 %a)
  %b_dbl = call double @__val_untag_float(i64 %b)
  %num_eq = fcmp oeq double %a_dbl, %b_dbl
  br i1 %num_eq, label %equal, label %not_equal

ptr_compare:
  ; Both are pointer-tagged — do strcmp
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

; __any_ne: Deep inequality (complement of __any_eq)
define i64 @__any_ne(i64 %a, i64 %b) {
entry:
  %eq = call i64 @__any_eq(i64 %a, i64 %b)
  %ne = xor i64 %eq, 1
  ret i64 %ne
}

; =============================================================================
; __any_length — Runtime-dispatched length for Any-typed values
; =============================================================================
; Returns the length of a list (count), map (count), or string (strlen).

define i64 @__any_length(i64 %val) {
entry:
  ; Check for nil
  %is_nil = call i1 @__val_is_nil(i64 %val)
  br i1 %is_nil, label %ret_zero, label %check_ptr

check_ptr:
  ; Must be pointer-tagged to have length
  %is_ptr = call i1 @__val_is_ptr(i64 %val)
  br i1 %is_ptr, label %do_ptr, label %ret_zero

do_ptr:
  %raw_ptr = call i8* @__val_untag_ptr(i64 %val)
  %ptr_int = ptrtoint i8* %raw_ptr to i64
  %is_null = icmp eq i64 %ptr_int, 0
  br i1 %is_null, label %ret_zero, label %check_type

check_type:
  ; Read type tag from ptr - 8
  %tag = call i64 @__gc_get_type_tag(i64 %ptr_int)
  %is_list = icmp eq i64 %tag, 2
  br i1 %is_list, label %do_list, label %check_map

check_map:
  %is_map = icmp eq i64 %tag, 3
  br i1 %is_map, label %do_map, label %do_string

do_list:
  ; List: count is at offset 0
  %list_ptr = inttoptr i64 %ptr_int to i64*
  %list_count = load i64, i64* %list_ptr
  ret i64 %list_count

do_map:
  ; Map: count is at offset 0
  %map_ptr = inttoptr i64 %ptr_int to i64*
  %map_count = load i64, i64* %map_ptr
  ret i64 %map_count

do_string:
  ; Default: treat as C string, use strlen
  %str_len = call i64 @strlen(i8* %raw_ptr)
  ret i64 %str_len

ret_zero:
  ret i64 0
}

; =============================================================================
; __list_length — Get count from a list struct (raw pointer, not NaN-boxed)
; =============================================================================

define i64 @__list_length(i64 %list) {
entry:
  %is_null = icmp eq i64 %list, 0
  br i1 %is_null, label %ret_zero, label %read
read:
  %ptr = inttoptr i64 %list to i64*
  %count = load i64, i64* %ptr
  ret i64 %count
ret_zero:
  ret i64 0
}

; =============================================================================
; Cooperative scheduler support
;
; Ports the ~10 __sched_* helpers from async_native.c to wasm32 IR. They are all
; trivial accessors plus a fixed-size result table; the only real difference from
; the C original is pointer width.
;
; POINTER WIDTH: a coroutine handle travels through Saffron as an i64 (every
; value is i64), but a wasm32 frame pointer is 32-bit. The codegen produces the
; handle with `ptrtoint ptr %frame to i64`, which zero-extends, so the top 32
; bits are always clear and truncating back is lossless. Each helper truncates
; explicitly rather than relying on inttoptr's implicit narrowing, so the
; intent is visible at the point it matters.
; =============================================================================

define i64 @__sched_get_yield_reason() {
entry:
  %v = load i64, i64* @__yield_reason
  ret i64 %v
}

define i64 @__sched_get_yield_arg() {
entry:
  %v = load i64, i64* @__yield_arg
  ret i64 %v
}

define i64 @__sched_get_task_result() {
entry:
  %v = load i64, i64* @__task_result
  ret i64 %v
}

define void @__sched_reset_yield() {
entry:
  store i64 0, i64* @__yield_reason
  store i64 0, i64* @__yield_arg
  ret void
}

; --- Per-task result storage ---
; Fixed 256-entry (handle, value) table, matching async_native.c. Linear scan
; from the newest entry backwards so a re-used handle resolves to its latest
; result.
@__task_res_handles = global [256 x i64] zeroinitializer
@__task_res_values = global [256 x i64] zeroinitializer
@__task_res_count = global i32 0

define void @__sched_store_result(i64 %handle, i64 %value) {
entry:
  %n = load i32, i32* @__task_res_count
  %full = icmp sge i32 %n, 256
  br i1 %full, label %done, label %store
store:
  %hp = getelementptr [256 x i64], [256 x i64]* @__task_res_handles, i32 0, i32 %n
  store i64 %handle, i64* %hp
  %vp = getelementptr [256 x i64], [256 x i64]* @__task_res_values, i32 0, i32 %n
  store i64 %value, i64* %vp
  %n1 = add i32 %n, 1
  store i32 %n1, i32* @__task_res_count
  br label %done
done:
  ret void
}

define i64 @__sched_get_stored_result(i64 %handle) {
entry:
  %n = load i32, i32* @__task_res_count
  %i0 = sub i32 %n, 1
  br label %loop
loop:
  %i = phi i32 [ %i0, %entry ], [ %inext, %next ]
  %in_range = icmp sge i32 %i, 0
  br i1 %in_range, label %check, label %not_found
check:
  %hp = getelementptr [256 x i64], [256 x i64]* @__task_res_handles, i32 0, i32 %i
  %h = load i64, i64* %hp
  %match = icmp eq i64 %h, %handle
  br i1 %match, label %found, label %next
found:
  %vp = getelementptr [256 x i64], [256 x i64]* @__task_res_values, i32 0, i32 %i
  %v = load i64, i64* %vp
  ret i64 %v
next:
  %inext = sub i32 %i, 1
  br label %loop
not_found:
  ret i64 0
}

define i64 @__sched_has_stored_result(i64 %handle) {
entry:
  %n = load i32, i32* @__task_res_count
  %i0 = sub i32 %n, 1
  br label %loop
loop:
  %i = phi i32 [ %i0, %entry ], [ %inext, %next ]
  %in_range = icmp sge i32 %i, 0
  br i1 %in_range, label %check, label %not_found
check:
  %hp = getelementptr [256 x i64], [256 x i64]* @__task_res_handles, i32 0, i32 %i
  %h = load i64, i64* %hp
  %match = icmp eq i64 %h, %handle
  br i1 %match, label %found, label %next
found:
  ret i64 1
next:
  %inext = sub i32 %i, 1
  br label %loop
not_found:
  ret i64 0
}

; --- Coroutine frame access ---
; After CoroSplit the frame layout is:
;   offset 0: resume function pointer
;   offset 4: destroy function pointer   (offset 8 on 64-bit hosts)
; A null resume pointer means the coroutine has run to completion, which is what
; llvm.coro.done tests.
;
; Note the offsets differ from async_native.c: that file indexes a
; coro_fn_t array, which is 8-byte-strided on the host and 4-byte-strided here.

define void @__sched_coro_resume(i64 %hdl) {
entry:
  %h32 = trunc i64 %hdl to i32
  %null = icmp eq i32 %h32, 0
  br i1 %null, label %done, label %load_fn
load_fn:
  %frame = inttoptr i32 %h32 to ptr
  %fn = load ptr, ptr %frame
  %fn_null = icmp eq ptr %fn, null
  br i1 %fn_null, label %done, label %call_fn
call_fn:
  call void %fn(ptr %frame)
  br label %done
done:
  ret void
}

define i64 @__sched_coro_done(i64 %hdl) {
entry:
  %h32 = trunc i64 %hdl to i32
  %null = icmp eq i32 %h32, 0
  br i1 %null, label %is_done, label %load_fn
load_fn:
  %frame = inttoptr i32 %h32 to ptr
  %fn = load ptr, ptr %frame
  %fn_null = icmp eq ptr %fn, null
  br i1 %fn_null, label %is_done, label %not_done
is_done:
  ret i64 1
not_done:
  ret i64 0
}

define void @__sched_coro_destroy(i64 %hdl) {
entry:
  %h32 = trunc i64 %hdl to i32
  %null = icmp eq i32 %h32, 0
  br i1 %null, label %done, label %load_fn
load_fn:
  %frame = inttoptr i32 %h32 to ptr
  ; destroy pointer sits one pointer-width in: 4 bytes on wasm32.
  %dp = getelementptr i8, ptr %frame, i32 4
  %fn = load ptr, ptr %dp
  %fn_null = icmp eq ptr %fn, null
  br i1 %fn_null, label %done, label %call_fn
call_fn:
  call void %fn(ptr %frame)
  br label %done
done:
  ret void
}

; --- Host-only helpers that the wasm build still references ---

; The native runtime interns strings so identical literals share one allocation.
; Returning the input unchanged is safe here: @__string_eq (above) compares
; contents with a real strcmp rather than relying on pointer identity, so
; skipping the intern table costs memory, never correctness.
define i64 @__string_intern(i64 %str) {
entry:
  ret i64 %str
}

; Native builds print the source location alongside a runtime error. There is no
; stderr in a bare wasm module, so this is a no-op; errors surface through the JS
; glue's js_log_str instead.
define void @__print_debug_location() {
entry:
  ret void
}

; wasm has a real trap instruction, so this needs no host support. It was
; previously only declared, which left it undefined at link time.
define void @__builtin_trap() {
entry:
  call void @llvm.trap()
  unreachable
}

declare void @llvm.trap()

; --- Monotonic clock ---
; Supplied by the JS glue (performance.now() / 1000). Declared, not defined:
; there is no clock_gettime in a bare wasm32 module.
declare double @js_time_now()

define double @sf_time_now() {
entry:
  %t = call double @js_time_now()
  ret double %t
}

; --- Socket readiness ---
; There are no file descriptors in a browser. The scheduler calls this when it
; has IO waiters; returning 0 (never ready) means an fd-parked task stays parked.
; Browser IO arrives through JS callbacks instead — see the yield-reason 6 path
; in the scheduler pump.
define i64 @sf_tcp_poll(i64 %fd, i64 %events, i64 %timeout_ms) {
entry:
  ret i64 0
}

; =============================================================================
; Scheduler pump — the JS interop entry point
;
; The native driver spins `while (scheduler_tick() == 1) {}`, which would freeze
; a browser tab. But scheduler_tick() was written to be called one step at a
; time: it returns 1 while work remains, 0 when the queues are drained. So the
; browser's own event loop can drive it:
;
;   function pump() {
;     if (wasm.exports.__sched_pump() === 1) queueMicrotask(pump)
;   }
;
; That makes the event loop the scheduler loop. Exported here so the JS glue
; does not need to know the module-prefixed name of the Saffron function.
; =============================================================================

; Weak, so a program that never imports "@async" still links: the scheduler
; function simply is not present, the reference resolves to null, and the pump
; reports "no work". A plain `declare` would make every non-async WASM build fail
; to link on a symbol it has no reason to provide.
declare extern_weak i64 @stdlib_scheduler_scheduler_tick()

define i64 @__sched_pump() {
entry:
  %absent = icmp eq ptr @stdlib_scheduler_scheduler_tick, null
  br i1 %absent, label %no_sched, label %tick
tick:
  %more = call i64 @stdlib_scheduler_scheduler_tick()
  ret i64 %more
no_sched:
  ret i64 0
}

; =============================================================================
; Entry point wrapper
; WASM entry point -- initializes heap, then calls __saffron_entry.
; =============================================================================

; Calls __saffron_boot, not __saffron_entry directly. Once a program contains a
; suspend point its entry point is emitted as `ptr @__saffron_entry()
; presplitcoroutine` — it returns a coroutine frame, not a value — and calling it
; through an `i64` signature traps. The codegen-emitted boot shim has a stable
; `i64 ()` signature either way and, for the coroutine case, enqueues the frame on
; the scheduler rather than treating it as a result.
declare i64 @__saffron_boot()

define void @_start() {
entry:
  ; Initialize heap pointer from linker-provided __heap_base symbol.
  ; On wasm32, ptrtoint gives a 32-bit value.
  %hb_ptr = ptrtoint i8* @__heap_base to i32
  ; Align to 8 bytes
  %aligned = add i32 %hb_ptr, 7
  %heap_start = and i32 %aligned, -8
  store i32 %heap_start, i32* @__heap_ptr
  call i64 @__saffron_boot()
  ret void
}
