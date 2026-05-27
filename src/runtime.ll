; Saffron Runtime Library
; Linked with every native program produced by the Saffron compiler.
;
; Usage: clang -O2 -o program program.ll src/runtime.ll

target triple = "arm64-apple-macosx15.0.0"

; --- Struct types ---
%List = type { i64, i64, i64* }
%Map = type { i64, i64, i64*, i64* }
%SB = type { i64, i64, i8* }

; --- External C declarations ---
declare i32 @puts(i8*)
declare i32 @printf(i8*, ...)
declare i32 @snprintf(i8*, i64, i8*, ...)
declare i8* @malloc(i64)
declare i8* @realloc(i8*, i64)
declare void @free(i8*)
declare i64 @strlen(i8*)
declare i8* @strcpy(i8*, i8*)
declare i8* @strcat(i8*, i8*)
declare i8* @strstr(i8*, i8*)
declare i32 @strcmp(i8*, i8*)
declare i32 @strncmp(i8*, i8*, i64)
declare i64 @atol(i8*)
declare void @exit(i32)
declare i8* @fopen(i8*, i8*)
declare i64 @fread(i8*, i64, i64, i8*)
declare i64 @fwrite(i8*, i64, i64, i8*)
declare i32 @fclose(i8*)
declare i32 @fseek(i8*, i64, i32)
declare i64 @ftell(i8*)
declare i8* @strtok(i8*, i8*)
declare i32 @pclose(i8*)
declare i8* @popen(i8*, i8*)
declare i8* @fgets(i8*, i32, i8*)
declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)
declare i32 @setjmp(i8*)
declare void @longjmp(i8*, i32)
declare i32 @access(i8*, i32)
declare i32 @mkdir(i8*, i32)
declare i8* @getcwd(i8*, i64)
declare i8* @opendir(i8*)
declare i8* @readdir(i8*)
declare i32 @closedir(i8*)
declare i8* @getenv(i8*)
declare i8* @strdup(i8*)

; --- Global constants ---
@.fmt.ld = linkonce_odr unnamed_addr constant [4 x i8] c"%ld\00"
@.str.empty = linkonce_odr unnamed_addr constant [1 x i8] c"\00"
@.str.rb = linkonce_odr unnamed_addr constant [2 x i8] c"r\00"
@.str.wb = linkonce_odr unnamed_addr constant [2 x i8] c"w\00"
@.str.r = linkonce_odr unnamed_addr constant [2 x i8] c"r\00"

; --- Global state ---
@__argc = weak global i32 0
@__argv = weak global i8** null
@__exception_value = weak global i64 0
@__jmp_buf_stack = weak global [64 x i8] zeroinitializer

; --- Async scheduler globals ---
@__yield_reason = global i64 0
@__yield_arg = global i64 0

; --- Async native helpers (defined in async_native.c) ---
declare double @sf_time_now()
declare i64 @sf_select_fds(i64*, i64, i64*, i64, i64)

; =============================================================================
; List Runtime
; =============================================================================

define i64 @__list_new() {
entry:
  %raw = call i8* @malloc(i64 24)
  %ptr = bitcast i8* %raw to %List*
  %count_ptr = getelementptr %List, %List* %ptr, i32 0, i32 0
  store i64 0, i64* %count_ptr
  %cap_ptr = getelementptr %List, %List* %ptr, i32 0, i32 1
  store i64 8, i64* %cap_ptr
  %data_raw = call i8* @malloc(i64 64)
  %data = bitcast i8* %data_raw to i64*
  %data_ptr = getelementptr %List, %List* %ptr, i32 0, i32 2
  store i64* %data, i64** %data_ptr
  %result = ptrtoint %List* %ptr to i64
  ret i64 %result
}

define void @__list_push(i64 %list, i64 %value) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %count_ptr = getelementptr %List, %List* %ptr, i32 0, i32 0
  %count = load i64, i64* %count_ptr
  %cap_ptr = getelementptr %List, %List* %ptr, i32 0, i32 1
  %cap = load i64, i64* %cap_ptr
  %need_grow = icmp sge i64 %count, %cap
  br i1 %need_grow, label %grow, label %store
grow:
  %new_cap = mul i64 %cap, 2
  store i64 %new_cap, i64* %cap_ptr
  %new_bytes = mul i64 %new_cap, 8
  %data_ptr_g = getelementptr %List, %List* %ptr, i32 0, i32 2
  %old_data = load i64*, i64** %data_ptr_g
  %old_raw = bitcast i64* %old_data to i8*
  %new_raw = call i8* @realloc(i8* %old_raw, i64 %new_bytes)
  %new_data = bitcast i8* %new_raw to i64*
  store i64* %new_data, i64** %data_ptr_g
  br label %store
store:
  %data_ptr_s = getelementptr %List, %List* %ptr, i32 0, i32 2
  %data = load i64*, i64** %data_ptr_s
  %slot = getelementptr i64, i64* %data, i64 %count
  store i64 %value, i64* %slot
  %new_count = add i64 %count, 1
  store i64 %new_count, i64* %count_ptr
  ret void
}

define i64 @__list_get(i64 %list, i64 %index) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %data_ptr = getelementptr %List, %List* %ptr, i32 0, i32 2
  %data = load i64*, i64** %data_ptr
  %slot = getelementptr i64, i64* %data, i64 %index
  %val = load i64, i64* %slot
  ret i64 %val
}

define void @__list_set(i64 %list, i64 %index, i64 %value) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %data_ptr = getelementptr %List, %List* %ptr, i32 0, i32 2
  %data = load i64*, i64** %data_ptr
  %slot = getelementptr i64, i64* %data, i64 %index
  store i64 %value, i64* %slot
  ret void
}

define i64 @__list_length(i64 %list) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %count_ptr = getelementptr %List, %List* %ptr, i32 0, i32 0
  %count = load i64, i64* %count_ptr
  ret i64 %count
}

define i64 @__list_pop(i64 %list) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %count_ptr = getelementptr %List, %List* %ptr, i32 0, i32 0
  %count = load i64, i64* %count_ptr
  %new_count = sub i64 %count, 1
  store i64 %new_count, i64* %count_ptr
  %data_ptr = getelementptr %List, %List* %ptr, i32 0, i32 2
  %data = load i64*, i64** %data_ptr
  %slot = getelementptr i64, i64* %data, i64 %new_count
  %val = load i64, i64* %slot
  ret i64 %val
}

; =============================================================================
; StringBuilder Runtime
; =============================================================================

define i64 @StringBuilder() {
entry:
  %raw = call i8* @malloc(i64 24)
  %ptr = bitcast i8* %raw to %SB*
  %lp = getelementptr %SB, %SB* %ptr, i32 0, i32 0
  store i64 0, i64* %lp
  %cp = getelementptr %SB, %SB* %ptr, i32 0, i32 1
  store i64 1024, i64* %cp
  %buf = call i8* @malloc(i64 1024)
  store i8 0, i8* %buf
  %bp = getelementptr %SB, %SB* %ptr, i32 0, i32 2
  store i8* %buf, i8** %bp
  %result = ptrtoint %SB* %ptr to i64
  ret i64 %result
}

define void @__sb_append(i64 %sb, i64 %str) {
entry:
  %ptr = inttoptr i64 %sb to %SB*
  %lp = getelementptr %SB, %SB* %ptr, i32 0, i32 0
  %len = load i64, i64* %lp
  %cp = getelementptr %SB, %SB* %ptr, i32 0, i32 1
  %cap = load i64, i64* %cp
  %bp = getelementptr %SB, %SB* %ptr, i32 0, i32 2
  %buf = load i8*, i8** %bp
  %s = inttoptr i64 %str to i8*
  %slen = call i64 @strlen(i8* %s)
  %needed = add i64 %len, %slen
  %needed1 = add i64 %needed, 1
  %fits = icmp slt i64 %needed1, %cap
  br i1 %fits, label %append, label %grow
grow:
  %new_cap = mul i64 %needed1, 2
  store i64 %new_cap, i64* %cp
  %new_buf = call i8* @realloc(i8* %buf, i64 %new_cap)
  store i8* %new_buf, i8** %bp
  br label %append
append:
  %cur_buf = load i8*, i8** %bp
  %dst = getelementptr i8, i8* %cur_buf, i64 %len
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %dst, i8* %s, i64 %slen, i1 false)
  %new_len = add i64 %len, %slen
  store i64 %new_len, i64* %lp
  %null_pos = getelementptr i8, i8* %cur_buf, i64 %new_len
  store i8 0, i8* %null_pos
  ret void
}

define i64 @__sb_to_string(i64 %sb) {
entry:
  %ptr = inttoptr i64 %sb to %SB*
  %bp = getelementptr %SB, %SB* %ptr, i32 0, i32 2
  %buf = load i8*, i8** %bp
  %result = ptrtoint i8* %buf to i64
  ret i64 %result
}

; =============================================================================
; Map Runtime
; =============================================================================

define i32 @__safe_strcmp(i8* %a, i8* %b) {
entry:
  %a_null = icmp eq i8* %a, null
  br i1 %a_null, label %neq, label %check_b
check_b:
  %b_null = icmp eq i8* %b, null
  br i1 %b_null, label %neq, label %do_cmp
do_cmp:
  %r = call i32 @strcmp(i8* %a, i8* %b)
  ret i32 %r
neq:
  ret i32 1
}

define i64 @__map_new() {
entry:
  %raw = call i8* @malloc(i64 32)
  %ptr = bitcast i8* %raw to %Map*
  %cp = getelementptr %Map, %Map* %ptr, i32 0, i32 0
  store i64 0, i64* %cp
  %cap = getelementptr %Map, %Map* %ptr, i32 0, i32 1
  store i64 16, i64* %cap
  %kr = call i8* @malloc(i64 128)
  %keys = bitcast i8* %kr to i64*
  %kp = getelementptr %Map, %Map* %ptr, i32 0, i32 2
  store i64* %keys, i64** %kp
  %vr = call i8* @malloc(i64 128)
  %vals = bitcast i8* %vr to i64*
  %vp = getelementptr %Map, %Map* %ptr, i32 0, i32 3
  store i64* %vals, i64** %vp
  %result = ptrtoint %Map* %ptr to i64
  ret i64 %result
}

define void @__map_set(i64 %map, i64 %key, i64 %value) {
entry:
  %ptr = inttoptr i64 %map to %Map*
  %cp = getelementptr %Map, %Map* %ptr, i32 0, i32 0
  %count = load i64, i64* %cp
  %cap_p = getelementptr %Map, %Map* %ptr, i32 0, i32 1
  %cap = load i64, i64* %cap_p
  %kp = getelementptr %Map, %Map* %ptr, i32 0, i32 2
  %keys = load i64*, i64** %kp
  %vp = getelementptr %Map, %Map* %ptr, i32 0, i32 3
  %vals = load i64*, i64** %vp
  %key_ptr = inttoptr i64 %key to i8*
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %check_grow, label %check
check:
  %kslot = getelementptr i64, i64* %keys, i64 %i
  %kval = load i64, i64* %kslot
  %kstr = inttoptr i64 %kval to i8*
  %cmp = call i32 @__safe_strcmp(i8* %key_ptr, i8* %kstr)
  %eq = icmp eq i32 %cmp, 0
  br i1 %eq, label %update, label %cont
cont:
  %next = add i64 %i, 1
  br label %loop
update:
  %vslot_u = getelementptr i64, i64* %vals, i64 %i
  store i64 %value, i64* %vslot_u
  ret void
check_grow:
  %need_grow = icmp sge i64 %count, %cap
  br i1 %need_grow, label %grow, label %insert
grow:
  %new_cap = mul i64 %cap, 2
  store i64 %new_cap, i64* %cap_p
  %new_bytes = mul i64 %new_cap, 8
  %old_k_raw = bitcast i64* %keys to i8*
  %new_k_raw = call i8* @realloc(i8* %old_k_raw, i64 %new_bytes)
  %new_keys = bitcast i8* %new_k_raw to i64*
  store i64* %new_keys, i64** %kp
  %old_v_raw = bitcast i64* %vals to i8*
  %new_v_raw = call i8* @realloc(i8* %old_v_raw, i64 %new_bytes)
  %new_vals = bitcast i8* %new_v_raw to i64*
  store i64* %new_vals, i64** %vp
  br label %insert
insert:
  %cur_keys = load i64*, i64** %kp
  %cur_vals = load i64*, i64** %vp
  %kslot_i = getelementptr i64, i64* %cur_keys, i64 %count
  store i64 %key, i64* %kslot_i
  %vslot_i = getelementptr i64, i64* %cur_vals, i64 %count
  store i64 %value, i64* %vslot_i
  %new_count = add i64 %count, 1
  store i64 %new_count, i64* %cp
  ret void
}

define i64 @__map_get(i64 %map, i64 %key) {
entry:
  %ptr = inttoptr i64 %map to %Map*
  %cp = getelementptr %Map, %Map* %ptr, i32 0, i32 0
  %count = load i64, i64* %cp
  %kp = getelementptr %Map, %Map* %ptr, i32 0, i32 2
  %keys = load i64*, i64** %kp
  %vp = getelementptr %Map, %Map* %ptr, i32 0, i32 3
  %vals = load i64*, i64** %vp
  %key_ptr = inttoptr i64 %key to i8*
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %notfound, label %check
check:
  %kslot = getelementptr i64, i64* %keys, i64 %i
  %kval = load i64, i64* %kslot
  %kstr = inttoptr i64 %kval to i8*
  %cmp = call i32 @__safe_strcmp(i8* %key_ptr, i8* %kstr)
  %eq = icmp eq i32 %cmp, 0
  br i1 %eq, label %found, label %cont
cont:
  %next = add i64 %i, 1
  br label %loop
found:
  %vslot = getelementptr i64, i64* %vals, i64 %i
  %val = load i64, i64* %vslot
  ret i64 %val
notfound:
  ret i64 0
}

define i64 @__map_has(i64 %map, i64 %key) {
entry:
  %ptr = inttoptr i64 %map to %Map*
  %cp = getelementptr %Map, %Map* %ptr, i32 0, i32 0
  %count = load i64, i64* %cp
  %kp = getelementptr %Map, %Map* %ptr, i32 0, i32 2
  %keys = load i64*, i64** %kp
  %key_ptr = inttoptr i64 %key to i8*
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %no, label %check
check:
  %kslot = getelementptr i64, i64* %keys, i64 %i
  %kval = load i64, i64* %kslot
  %kstr = inttoptr i64 %kval to i8*
  %cmp = call i32 @__safe_strcmp(i8* %key_ptr, i8* %kstr)
  %eq = icmp eq i32 %cmp, 0
  br i1 %eq, label %yes, label %cont
cont:
  %next = add i64 %i, 1
  br label %loop
yes:
  ret i64 1
no:
  ret i64 0
}

define i64 @__map_keys(i64 %map) {
entry:
  %ptr = inttoptr i64 %map to %Map*
  %cp = getelementptr %Map, %Map* %ptr, i32 0, i32 0
  %count = load i64, i64* %cp
  %kp = getelementptr %Map, %Map* %ptr, i32 0, i32 2
  %keys = load i64*, i64** %kp
  %list = call i64 @__list_new()
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %end, label %cont
cont:
  %kslot = getelementptr i64, i64* %keys, i64 %i
  %kval = load i64, i64* %kslot
  call void @__list_push(i64 %list, i64 %kval)
  %next = add i64 %i, 1
  br label %loop
end:
  ret i64 %list
}

; =============================================================================
; Extra Runtime (strings, I/O, OS)
; =============================================================================

define i64 @__str_ends_with(i8* %str, i8* %suffix) {
entry:
  %slen = call i64 @strlen(i8* %str)
  %plen = call i64 @strlen(i8* %suffix)
  %too_short = icmp sgt i64 %plen, %slen
  br i1 %too_short, label %no, label %check
check:
  %offset = sub i64 %slen, %plen
  %tail = getelementptr i8, i8* %str, i64 %offset
  %cmp = call i32 @strcmp(i8* %tail, i8* %suffix)
  %eq = icmp eq i32 %cmp, 0
  %result = zext i1 %eq to i64
  ret i64 %result
no:
  ret i64 0
}

define i64 @__str_split(i8* %str, i8* %delim) {
entry:
  %list = call i64 @__list_new()
  %slen = call i64 @strlen(i8* %str)
  %bufsize = add i64 %slen, 1
  %buf = call i8* @malloc(i64 %bufsize)
  call i8* @strcpy(i8* %buf, i8* %str)
  %dlen = call i64 @strlen(i8* %delim)
  br label %loop
loop:
  %pos = phi i8* [%buf, %entry], [%next_pos, %found]
  %poslen = call i64 @strlen(i8* %pos)
  %is_empty = icmp eq i64 %poslen, 0
  br i1 %is_empty, label %end, label %search
search:
  %match = call i8* @strstr(i8* %pos, i8* %delim)
  %has_match = icmp ne i8* %match, null
  br i1 %has_match, label %found, label %last
found:
  store i8 0, i8* %match
  %tok_len = call i64 @strlen(i8* %pos)
  %tok_buf = call i8* @malloc(i64 %bufsize)
  call i8* @strcpy(i8* %tok_buf, i8* %pos)
  %tok_val = ptrtoint i8* %tok_buf to i64
  call void @__list_push(i64 %list, i64 %tok_val)
  %next_pos = getelementptr i8, i8* %match, i64 %dlen
  br label %loop
last:
  %last_buf = call i8* @malloc(i64 %bufsize)
  call i8* @strcpy(i8* %last_buf, i8* %pos)
  %last_val = ptrtoint i8* %last_buf to i64
  call void @__list_push(i64 %list, i64 %last_val)
  br label %end
end:
  ret i64 %list
}

define i64 @__str_replace(i8* %str, i8* %old, i8* %new) {
entry:
  %slen = call i64 @strlen(i8* %str)
  %olen = call i64 @strlen(i8* %old)
  %nlen = call i64 @strlen(i8* %new)
  %init_size = add i64 %slen, 1
  %init_cap = mul i64 %init_size, 2
  %buf_p = alloca i8*
  %len_p = alloca i64
  %cap_p = alloca i64
  %init_buf = call i8* @malloc(i64 %init_cap)
  store i8 0, i8* %init_buf
  store i8* %init_buf, i8** %buf_p
  store i64 0, i64* %len_p
  store i64 %init_cap, i64* %cap_p
  br label %loop
loop:
  %src = phi i8* [%str, %entry], [%after, %do_replace]
  %src_len = call i64 @strlen(i8* %src)
  %is_done = icmp eq i64 %src_len, 0
  br i1 %is_done, label %end, label %search
search:
  %match = call i8* @strstr(i8* %src, i8* %old)
  %found = icmp ne i8* %match, null
  br i1 %found, label %do_replace, label %copy_rest
do_replace:
  %prefix_len = ptrtoint i8* %match to i64
  %src_int = ptrtoint i8* %src to i64
  %plen = sub i64 %prefix_len, %src_int
  call void @__join_append(i8** %buf_p, i64* %len_p, i64* %cap_p, i8* %src, i64 %plen)
  call void @__join_append(i8** %buf_p, i64* %len_p, i64* %cap_p, i8* %new, i64 %nlen)
  %after = getelementptr i8, i8* %match, i64 %olen
  br label %loop
copy_rest:
  call void @__join_append(i8** %buf_p, i64* %len_p, i64* %cap_p, i8* %src, i64 %src_len)
  br label %end
end:
  %final = load i8*, i8** %buf_p
  %rv = ptrtoint i8* %final to i64
  ret i64 %rv
}

define i64 @__int_to_string(i64 %val) {
entry:
  %buf = call i8* @malloc(i64 32)
  %fmt = getelementptr [4 x i8], [4 x i8]* @.fmt.ld, i64 0, i64 0
  call i32 (i8*, i64, i8*, ...) @snprintf(i8* %buf, i64 32, i8* %fmt, i64 %val)
  %result = ptrtoint i8* %buf to i64
  ret i64 %result
}

define i64 @__list_join(i64 %list, i8* %sep) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %cp = getelementptr %List, %List* %ptr, i32 0, i32 0
  %count = load i64, i64* %cp
  %dp = getelementptr %List, %List* %ptr, i32 0, i32 2
  %data = load i64*, i64** %dp
  %sep_len = call i64 @strlen(i8* %sep)
  %init_cap = alloca i64
  store i64 1024, i64* %init_cap
  %init_buf = call i8* @malloc(i64 1024)
  store i8 0, i8* %init_buf
  %buf_p = alloca i8*
  store i8* %init_buf, i8** %buf_p
  %len_p = alloca i64
  store i64 0, i64* %len_p
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %end, label %body
body:
  %need_sep = icmp sgt i64 %i, 0
  br i1 %need_sep, label %add_sep, label %add_elem
add_sep:
  call void @__join_append(i8** %buf_p, i64* %len_p, i64* %init_cap, i8* %sep, i64 %sep_len)
  br label %add_elem
add_elem:
  %slot = getelementptr i64, i64* %data, i64 %i
  %elem = load i64, i64* %slot
  %elem_ptr = inttoptr i64 %elem to i8*
  %elem_len = call i64 @strlen(i8* %elem_ptr)
  call void @__join_append(i8** %buf_p, i64* %len_p, i64* %init_cap, i8* %elem_ptr, i64 %elem_len)
  br label %cont
cont:
  %next = add i64 %i, 1
  br label %loop
end:
  %final_buf = load i8*, i8** %buf_p
  %result = ptrtoint i8* %final_buf to i64
  ret i64 %result
}

define void @__join_append(i8** %buf_p, i64* %len_p, i64* %cap_p, i8* %src, i64 %src_len) {
entry:
  %len = load i64, i64* %len_p
  %cap = load i64, i64* %cap_p
  %buf = load i8*, i8** %buf_p
  %needed = add i64 %len, %src_len
  %needed1 = add i64 %needed, 1
  %fits = icmp slt i64 %needed1, %cap
  br i1 %fits, label %do_copy, label %grow
grow:
  %new_cap = mul i64 %needed1, 2
  store i64 %new_cap, i64* %cap_p
  %new_buf = call i8* @realloc(i8* %buf, i64 %new_cap)
  store i8* %new_buf, i8** %buf_p
  br label %do_copy
do_copy:
  %cur_buf = load i8*, i8** %buf_p
  %dst = getelementptr i8, i8* %cur_buf, i64 %len
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %dst, i8* %src, i64 %src_len, i1 false)
  %new_len = add i64 %len, %src_len
  store i64 %new_len, i64* %len_p
  %null_pos = getelementptr i8, i8* %cur_buf, i64 %new_len
  store i8 0, i8* %null_pos
  ret void
}

define i64 @__list_contains(i64 %list, i64 %val) {
entry:
  %ptr = inttoptr i64 %list to %List*
  %cp = getelementptr %List, %List* %ptr, i32 0, i32 0
  %count = load i64, i64* %cp
  %dp = getelementptr %List, %List* %ptr, i32 0, i32 2
  %data = load i64*, i64** %dp
  %val_ptr = inttoptr i64 %val to i8*
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %no_match]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %not_found, label %check
check:
  %slot = getelementptr i64, i64* %data, i64 %i
  %elem = load i64, i64* %slot
  %elem_ptr = inttoptr i64 %elem to i8*
  %cmp = call i32 @__safe_strcmp(i8* %val_ptr, i8* %elem_ptr)
  %eq = icmp eq i32 %cmp, 0
  br i1 %eq, label %found, label %no_match
no_match:
  %next = add i64 %i, 1
  br label %loop
found:
  ret i64 1
not_found:
  ret i64 0
}

define i64 @__io_read_file(i8* %path) {
entry:
  %mode = getelementptr [2 x i8], [2 x i8]* @.str.rb, i64 0, i64 0
  %fp = call i8* @fopen(i8* %path, i8* %mode)
  %is_null = icmp eq i8* %fp, null
  br i1 %is_null, label %fail, label %opened
opened:
  call i32 @fseek(i8* %fp, i64 0, i32 2)
  %size = call i64 @ftell(i8* %fp)
  call i32 @fseek(i8* %fp, i64 0, i32 0)
  %bufsize = add i64 %size, 1
  %buf = call i8* @malloc(i64 %bufsize)
  call i64 @fread(i8* %buf, i64 1, i64 %size, i8* %fp)
  %null_pos = getelementptr i8, i8* %buf, i64 %size
  store i8 0, i8* %null_pos
  call i32 @fclose(i8* %fp)
  %result = ptrtoint i8* %buf to i64
  ret i64 %result
fail:
  %empty = call i8* @malloc(i64 1)
  store i8 0, i8* %empty
  %r2 = ptrtoint i8* %empty to i64
  ret i64 %r2
}

define void @__io_write_file(i8* %path, i8* %content) {
entry:
  %mode = getelementptr [2 x i8], [2 x i8]* @.str.wb, i64 0, i64 0
  %fp = call i8* @fopen(i8* %path, i8* %mode)
  %is_null = icmp eq i8* %fp, null
  br i1 %is_null, label %end, label %write
write:
  %len = call i64 @strlen(i8* %content)
  call i64 @fwrite(i8* %content, i64 1, i64 %len, i8* %fp)
  call i32 @fclose(i8* %fp)
  br label %end
end:
  ret void
}

define i64 @__io_file_exists(i8* %path) {
entry:
  %rc = call i32 @access(i8* %path, i32 0)
  %exists = icmp eq i32 %rc, 0
  %result = select i1 %exists, i64 1, i64 0
  ret i64 %result
}

define void @__io_mkdir(i8* %path) {
entry:
  call i32 @mkdir(i8* %path, i32 493)
  ret void
}

@.str.slash = linkonce_odr unnamed_addr constant [2 x i8] c"/\00"
@.str.macos = linkonce_odr unnamed_addr constant [6 x i8] c"macos\00"

define i64 @__os_cwd() {
entry:
  %buf = call i8* @malloc(i64 4096)
  %result = call i8* @getcwd(i8* %buf, i64 4096)
  %is_null = icmp eq i8* %result, null
  br i1 %is_null, label %fail, label %ok
ok:
  %r = ptrtoint i8* %buf to i64
  ret i64 %r
fail:
  store i8 0, i8* %buf
  %r2 = ptrtoint i8* %buf to i64
  ret i64 %r2
}

define i64 @__os_path_sep() {
entry:
  %ptr = getelementptr [2 x i8], [2 x i8]* @.str.slash, i64 0, i64 0
  %dup = call i8* @strdup(i8* %ptr)
  %r = ptrtoint i8* %dup to i64
  ret i64 %r
}

define i64 @__os_platform() {
entry:
  %ptr = getelementptr [6 x i8], [6 x i8]* @.str.macos, i64 0, i64 0
  %dup = call i8* @strdup(i8* %ptr)
  %r = ptrtoint i8* %dup to i64
  ret i64 %r
}

define i64 @__os_env(i8* %name) {
entry:
  %val = call i8* @getenv(i8* %name)
  %is_null = icmp eq i8* %val, null
  br i1 %is_null, label %empty, label %found
found:
  %dup = call i8* @strdup(i8* %val)
  %r = ptrtoint i8* %dup to i64
  ret i64 %r
empty:
  %buf = call i8* @malloc(i64 1)
  store i8 0, i8* %buf
  %r2 = ptrtoint i8* %buf to i64
  ret i64 %r2
}

define i64 @__os_exec(i8* %cmd) {
entry:
  %mode = getelementptr [2 x i8], [2 x i8]* @.str.r, i64 0, i64 0
  %fp = call i8* @popen(i8* %cmd, i8* %mode)
  %is_null = icmp eq i8* %fp, null
  br i1 %is_null, label %fail, label %read
read:
  %buf_p = alloca i8*
  %len_p = alloca i64
  %cap_p = alloca i64
  %init_buf = call i8* @malloc(i64 4096)
  store i8 0, i8* %init_buf
  store i8* %init_buf, i8** %buf_p
  store i64 0, i64* %len_p
  store i64 4096, i64* %cap_p
  %tmp = call i8* @malloc(i64 4096)
  br label %read_loop
read_loop:
  %line = call i8* @fgets(i8* %tmp, i32 4096, i8* %fp)
  %eof = icmp eq i8* %line, null
  br i1 %eof, label %done, label %concat
concat:
  %chunk_len = call i64 @strlen(i8* %tmp)
  call void @__join_append(i8** %buf_p, i64* %len_p, i64* %cap_p, i8* %tmp, i64 %chunk_len)
  br label %read_loop
done:
  call void @free(i8* %tmp)
  call i32 @pclose(i8* %fp)
  %final = load i8*, i8** %buf_p
  %result = ptrtoint i8* %final to i64
  ret i64 %result
fail:
  %empty = call i8* @malloc(i64 1)
  store i8 0, i8* %empty
  %r2 = ptrtoint i8* %empty to i64
  ret i64 %r2
}

define i64 @__os_args() {
entry:
  %argc = load i32, i32* @__argc
  %argv = load i8**, i8*** @__argv
  %list = call i64 @__list_new()
  %count = sext i32 %argc to i64
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %body]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %end, label %body
body:
  %slot = getelementptr i8*, i8** %argv, i64 %i
  %arg = load i8*, i8** %slot
  %arg_int = ptrtoint i8* %arg to i64
  call void @__list_push(i64 %list, i64 %arg_int)
  %next = add i64 %i, 1
  br label %loop
end:
  ret i64 %list
}

; --- Runtime string methods (rt_str_*) ---

define i64 @rt_str_trim(i64 %s) {
entry:
  %ptr = inttoptr i64 %s to i8*
  %len = call i64 @strlen(i8* %ptr)
  %start_p = alloca i64
  %end_p = alloca i64
  store i64 0, i64* %start_p
  store i64 %len, i64* %end_p
  br label %trim_start
trim_start:
  %si = load i64, i64* %start_p
  %ei = load i64, i64* %end_p
  %s_done = icmp sge i64 %si, %ei
  br i1 %s_done, label %done, label %check_start
check_start:
  %sc = getelementptr i8, i8* %ptr, i64 %si
  %ch_s = load i8, i8* %sc
  %is_sp = icmp eq i8 %ch_s, 32
  %is_tab = icmp eq i8 %ch_s, 9
  %is_nl = icmp eq i8 %ch_s, 10
  %is_cr = icmp eq i8 %ch_s, 13
  %ws1 = or i1 %is_sp, %is_tab
  %ws2 = or i1 %ws1, %is_nl
  %ws3 = or i1 %ws2, %is_cr
  br i1 %ws3, label %inc_start, label %trim_end
inc_start:
  %si2 = add i64 %si, 1
  store i64 %si2, i64* %start_p
  br label %trim_start
trim_end:
  %ei2 = load i64, i64* %end_p
  %ei3 = sub i64 %ei2, 1
  %si3 = load i64, i64* %start_p
  %e_done = icmp slt i64 %ei3, %si3
  br i1 %e_done, label %done, label %check_end
check_end:
  %ec = getelementptr i8, i8* %ptr, i64 %ei3
  %ch_e = load i8, i8* %ec
  %is_sp2 = icmp eq i8 %ch_e, 32
  %is_tab2 = icmp eq i8 %ch_e, 9
  %is_nl2 = icmp eq i8 %ch_e, 10
  %is_cr2 = icmp eq i8 %ch_e, 13
  %ws4 = or i1 %is_sp2, %is_tab2
  %ws5 = or i1 %ws4, %is_nl2
  %ws6 = or i1 %ws5, %is_cr2
  br i1 %ws6, label %dec_end, label %done
dec_end:
  store i64 %ei3, i64* %end_p
  br label %trim_end
done:
  %fs = load i64, i64* %start_p
  %fe = load i64, i64* %end_p
  %new_len = sub i64 %fe, %fs
  %buf_sz = add i64 %new_len, 1
  %buf = call i8* @malloc(i64 %buf_sz)
  %src = getelementptr i8, i8* %ptr, i64 %fs
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %buf, i8* %src, i64 %new_len, i1 false)
  %term = getelementptr i8, i8* %buf, i64 %new_len
  store i8 0, i8* %term
  %r = ptrtoint i8* %buf to i64
  ret i64 %r
}

define i64 @rt_str_index_of(i64 %s, i64 %needle) {
entry:
  %str_ptr = inttoptr i64 %s to i8*
  %ndl_ptr = inttoptr i64 %needle to i8*
  %found = call i8* @strstr(i8* %str_ptr, i8* %ndl_ptr)
  %is_null = icmp eq i8* %found, null
  br i1 %is_null, label %not_found, label %calc
calc:
  %str_int = ptrtoint i8* %str_ptr to i64
  %found_int = ptrtoint i8* %found to i64
  %idx = sub i64 %found_int, %str_int
  ret i64 %idx
not_found:
  ret i64 -1
}

define i64 @rt_str_repeat(i64 %s, i64 %count) {
entry:
  %ptr = inttoptr i64 %s to i8*
  %len = call i64 @strlen(i8* %ptr)
  %total = mul i64 %len, %count
  %buf_sz = add i64 %total, 1
  %buf = call i8* @malloc(i64 %buf_sz)
  store i8 0, i8* %buf
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %body]
  %done = icmp sge i64 %i, %count
  br i1 %done, label %end, label %body
body:
  call i8* @strcat(i8* %buf, i8* %ptr)
  %next = add i64 %i, 1
  br label %loop
end:
  %r = ptrtoint i8* %buf to i64
  ret i64 %r
}

define i64 @rt_str_to_upper(i64 %s) {
entry:
  %ptr = inttoptr i64 %s to i8*
  %len = call i64 @strlen(i8* %ptr)
  %buf_sz = add i64 %len, 1
  %buf = call i8* @malloc(i64 %buf_sz)
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %len
  br i1 %done, label %end, label %body
body:
  %src_p = getelementptr i8, i8* %ptr, i64 %i
  %ch = load i8, i8* %src_p
  %is_lower = icmp uge i8 %ch, 97
  %is_lower2 = icmp ule i8 %ch, 122
  %lower = and i1 %is_lower, %is_lower2
  br i1 %lower, label %toupper, label %keep
toupper:
  %upper_ch = sub i8 %ch, 32
  br label %cont
keep:
  br label %cont
cont:
  %out_ch = phi i8 [%upper_ch, %toupper], [%ch, %keep]
  %dst_p = getelementptr i8, i8* %buf, i64 %i
  store i8 %out_ch, i8* %dst_p
  %next = add i64 %i, 1
  br label %loop
end:
  %term = getelementptr i8, i8* %buf, i64 %len
  store i8 0, i8* %term
  %r = ptrtoint i8* %buf to i64
  ret i64 %r
}

define i64 @rt_str_to_lower(i64 %s) {
entry:
  %ptr = inttoptr i64 %s to i8*
  %len = call i64 @strlen(i8* %ptr)
  %buf_sz = add i64 %len, 1
  %buf = call i8* @malloc(i64 %buf_sz)
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %cont]
  %done = icmp sge i64 %i, %len
  br i1 %done, label %end, label %body
body:
  %src_p = getelementptr i8, i8* %ptr, i64 %i
  %ch = load i8, i8* %src_p
  %is_upper = icmp uge i8 %ch, 65
  %is_upper2 = icmp ule i8 %ch, 90
  %upper = and i1 %is_upper, %is_upper2
  br i1 %upper, label %tolower, label %keep
tolower:
  %lower_ch = add i8 %ch, 32
  br label %cont
keep:
  br label %cont
cont:
  %out_ch = phi i8 [%lower_ch, %tolower], [%ch, %keep]
  %dst_p = getelementptr i8, i8* %buf, i64 %i
  store i8 %out_ch, i8* %dst_p
  %next = add i64 %i, 1
  br label %loop
end:
  %term = getelementptr i8, i8* %buf, i64 %len
  store i8 0, i8* %term
  %r = ptrtoint i8* %buf to i64
  ret i64 %r
}

; --- Runtime list methods (rt_list_*) ---

define i64 @rt_list_reverse(i64 %list) {
entry:
  %len = call i64 @__list_length(i64 %list)
  %new = call i64 @__list_new()
  %last = sub i64 %len, 1
  br label %loop
loop:
  %i = phi i64 [%last, %entry], [%next, %body]
  %done = icmp slt i64 %i, 0
  br i1 %done, label %end, label %body
body:
  %elem = call i64 @__list_get(i64 %list, i64 %i)
  call void @__list_push(i64 %new, i64 %elem)
  %next = sub i64 %i, 1
  br label %loop
end:
  ret i64 %new
}

define i64 @rt_list_copy(i64 %list) {
entry:
  %len = call i64 @__list_length(i64 %list)
  %new = call i64 @__list_new()
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %body]
  %done = icmp sge i64 %i, %len
  br i1 %done, label %end, label %body
body:
  %elem = call i64 @__list_get(i64 %list, i64 %i)
  call void @__list_push(i64 %new, i64 %elem)
  %next = add i64 %i, 1
  br label %loop
end:
  ret i64 %new
}

define i64 @rt_list_sort(i64 %list) {
entry:
  %len = call i64 @__list_length(i64 %list)
  %copy = call i64 @rt_list_copy(i64 %list)
  %len_1 = sub i64 %len, 1
  br label %outer
outer:
  %oi = phi i64 [0, %entry], [%onext, %outer_inc]
  %o_done = icmp sge i64 %oi, %len_1
  br i1 %o_done, label %end, label %inner_start
inner_start:
  %inner_max = sub i64 %len_1, %oi
  br label %inner
inner:
  %ji = phi i64 [0, %inner_start], [%jnext, %no_swap]
  %j_done = icmp sge i64 %ji, %inner_max
  br i1 %j_done, label %outer_inc, label %compare
compare:
  %j1 = add i64 %ji, 1
  %a = call i64 @__list_get(i64 %copy, i64 %ji)
  %b = call i64 @__list_get(i64 %copy, i64 %j1)
  %gt = icmp sgt i64 %a, %b
  br i1 %gt, label %swap, label %no_swap
swap:
  call void @__list_set(i64 %copy, i64 %ji, i64 %b)
  call void @__list_set(i64 %copy, i64 %j1, i64 %a)
  br label %no_swap
no_swap:
  %jnext = add i64 %ji, 1
  br label %inner
outer_inc:
  %onext = add i64 %oi, 1
  br label %outer
end:
  ret i64 %copy
}

; --- IO: walk_dir and append_file ---

@.str.a = linkonce_odr unnamed_addr constant [2 x i8] c"a\00"

define i64 @__io_walk_dir(i8* %path) {
entry:
  %dir = call i8* @opendir(i8* %path)
  %is_null = icmp eq i8* %dir, null
  %list = call i64 @__list_new()
  br i1 %is_null, label %done, label %loop
loop:
  %ent = call i8* @readdir(i8* %dir)
  %ent_null = icmp eq i8* %ent, null
  br i1 %ent_null, label %close, label %process
process:
  ; dirent.d_name is at offset 21 on macOS (after d_ino=8, d_seekoff=8, d_reclen=2, d_namlen=2, d_type=1)
  %name_ptr = getelementptr i8, i8* %ent, i64 21
  ; Skip . and ..
  %first = load i8, i8* %name_ptr
  %is_dot = icmp eq i8 %first, 46
  br i1 %is_dot, label %check_dotdot, label %add_entry
check_dotdot:
  %second_ptr = getelementptr i8, i8* %name_ptr, i64 1
  %second = load i8, i8* %second_ptr
  %is_nul = icmp eq i8 %second, 0
  br i1 %is_nul, label %loop, label %check_dd2
check_dd2:
  %is_dot2 = icmp eq i8 %second, 46
  %third_ptr = getelementptr i8, i8* %name_ptr, i64 2
  %third = load i8, i8* %third_ptr
  %third_nul = icmp eq i8 %third, 0
  %is_dd = and i1 %is_dot2, %third_nul
  br i1 %is_dd, label %loop, label %add_entry
add_entry:
  %len = call i64 @strlen(i8* %name_ptr)
  %buf_sz = add i64 %len, 1
  %buf = call i8* @malloc(i64 %buf_sz)
  call i8* @strcpy(i8* %buf, i8* %name_ptr)
  %str_int = ptrtoint i8* %buf to i64
  call void @__list_push(i64 %list, i64 %str_int)
  br label %loop
close:
  call i32 @closedir(i8* %dir)
  br label %done
done:
  ret i64 %list
}

define void @__io_append_file(i8* %path, i8* %content) {
entry:
  %mode = getelementptr [2 x i8], [2 x i8]* @.str.a, i64 0, i64 0
  %fp = call i8* @fopen(i8* %path, i8* %mode)
  %is_null = icmp eq i8* %fp, null
  br i1 %is_null, label %end, label %write
write:
  %len = call i64 @strlen(i8* %content)
  call i64 @fwrite(i8* %content, i64 1, i64 %len, i8* %fp)
  call i32 @fclose(i8* %fp)
  br label %end
end:
  ret void
}
