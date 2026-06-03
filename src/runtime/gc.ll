; =============================================================================
; Saffron Garbage Collector — Mark-and-Sweep with Shadow Stack
; =============================================================================
;
; Design:
;   - All GC-tracked allocations have a 24-byte header BEFORE the user pointer
;   - Header: { next_ptr: i64, info: i64, reserved: i64 }
;   - info encodes: mark_bit (bit 0), type_tag (bits 8-15), size (bits 16+)
;   - Linked list of all allocations via next_ptr
;   - Shadow stack tracks root addresses for mark phase
;
; Type tags:
;   0 = raw (opaque, no inner pointers)
;   1 = string (bytes, no inner pointers)
;   2 = list { count@0, capacity@8, data_ptr@16 }
;   3 = map { count@0, capacity@8, keys_ptr@16, values_ptr@24 }
;   4 = closure { fn_ptr@0, env_ptr@8 }
;   5 = class instance (N fields, all scanned as potential pointers)
;   6 = stringbuilder { len@0, cap@8, buf_ptr@16 }
;   7 = data array (inner buffer, all entries scanned)
;   8 = key/value array (inner buffer, all entries scanned)
;   9 = env (closure environment, all slots scanned)

target triple = "arm64-apple-macosx14.0.0"

; =============================================================================
; GC State Globals
; =============================================================================

@__gc_head = global i64 0            ; head of allocation linked list
@__gc_alloc_count = global i64 0     ; number of live allocations
@__gc_total_bytes = global i64 0     ; total bytes allocated (with headers)
@__gc_threshold = global i64 0       ; auto-collect threshold (0 = use default 64KB)
@__gc_enabled = global i64 0         ; 0=disabled, 1=enabled
@__gc_collections = global i64 0     ; collections performed
@__gc_freed_bytes = global i64 0     ; total bytes freed
@__gc_shadow_stack = global i64 0    ; pointer to shadow stack struct
@__gc_shadow_stack_inited = global i64 0  ; 0=not inited, 1=inited

; =============================================================================
; Generational GC — Nursery (Young Generation)
; =============================================================================
; The nursery is a fixed-size bump-allocation arena. Objects are allocated by
; simply incrementing a pointer (fastest possible allocation). When the nursery
; is full, a minor GC promotes live objects to the old generation (the existing
; mark-and-sweep heap). Most objects die young, so minor GC is fast.
;
; Layout: each nursery object has the same 24-byte header as old-gen objects,
; enabling uniform scanning and seamless promotion.

@__gc_nursery_start = global i64 0   ; base address of nursery arena
@__gc_nursery_ptr = global i64 0     ; current bump pointer (next free byte)
@__gc_nursery_end = global i64 0     ; one-past-end of nursery arena
@__gc_nursery_inited = global i64 0  ; 0=not inited, 1=inited
@__gc_nursery_size = global i64 262144  ; nursery size in bytes (256KB default)
@__gc_minor_collections = global i64 0  ; number of minor collections

; Remembered set: old-gen slots that point into the nursery.
; When an old-gen object stores a pointer to a nursery object, we record the
; address of the SLOT (not the object) so minor GC can find nursery references
; reachable only through old-gen objects.
; Struct: { count: i64 @0, capacity: i64 @8, data_ptr: i64 @16 }
@__gc_remembered_set = global i64 0  ; pointer to remembered set struct
@__gc_remembered_set_inited = global i64 0

; Constants
@.__gc_header_size = private constant i64 24

declare i8* @malloc(i64)
declare i8* @realloc(i8*, i64)
declare void @free(i8*)
declare i64 @write(i32, i8*, i64)

; =============================================================================
; Helper: pack info field
; info = mark_bit + (tag * 256) + (size * 65536)
; =============================================================================

define private i64 @__gc_pack_info(i64 %mark, i64 %tag, i64 %size) {
entry:
  %t1 = shl i64 %tag, 8
  %t2 = shl i64 %size, 16
  %r1 = or i64 %mark, %t1
  %r2 = or i64 %r1, %t2
  ret i64 %r2
}

; Extract mark bit from info
define private i64 @__gc_info_mark(i64 %info) {
entry:
  %r = and i64 %info, 1
  ret i64 %r
}

; Extract type tag from info
define private i64 @__gc_info_tag(i64 %info) {
entry:
  %shifted = lshr i64 %info, 8
  %r = and i64 %shifted, 255
  ret i64 %r
}

; Extract size from info
define private i64 @__gc_info_size(i64 %info) {
entry:
  %r = lshr i64 %info, 16
  ret i64 %r
}

; =============================================================================
; Shadow Stack Management
; Shadow stack struct: { count: i64 @0, capacity: i64 @8, data_ptr: i64 @16 }
; =============================================================================

define void @__gc_init_shadow_stack() {
entry:
  %inited = load i64, i64* @__gc_shadow_stack_inited
  %is_inited = icmp ne i64 %inited, 0
  br i1 %is_inited, label %done, label %init

init:
  ; Allocate shadow stack struct (24 bytes)
  %ss_raw = call i8* @malloc(i64 24)
  %ss = ptrtoint i8* %ss_raw to i64
  ; count = 0
  %ss_ptr = inttoptr i64 %ss to i64*
  store i64 0, i64* %ss_ptr
  ; capacity = 256
  %cap_addr = add i64 %ss, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 256, i64* %cap_ptr
  ; data = malloc(256 * 8 = 2048)
  %data_raw = call i8* @malloc(i64 2048)
  %data = ptrtoint i8* %data_raw to i64
  %data_addr = add i64 %ss, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  store i64 %data, i64* %data_ptr
  ; Store shadow stack pointer and mark as inited
  store i64 %ss, i64* @__gc_shadow_stack
  store i64 1, i64* @__gc_shadow_stack_inited
  br label %done

done:
  ret void
}

; Push a root address onto the shadow stack
define void @__gc_push_root(i64 %root_addr) {
entry:
  ; Ensure initialized
  %inited = load i64, i64* @__gc_shadow_stack_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %do_init, label %push

do_init:
  call void @__gc_init_shadow_stack()
  br label %push

push:
  %ss = load i64, i64* @__gc_shadow_stack
  ; Load count
  %count_ptr = inttoptr i64 %ss to i64*
  %count = load i64, i64* %count_ptr
  ; Load capacity
  %cap_addr = add i64 %ss, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  %cap = load i64, i64* %cap_ptr
  ; Check if need to grow
  %need_grow = icmp uge i64 %count, %cap
  br i1 %need_grow, label %grow, label %store

grow:
  %new_cap = shl i64 %cap, 1
  %new_bytes = shl i64 %new_cap, 3
  %data_addr_g = add i64 %ss, 16
  %data_ptr_g = inttoptr i64 %data_addr_g to i64*
  %old_data_g = load i64, i64* %data_ptr_g
  %old_data_raw = inttoptr i64 %old_data_g to i8*
  %new_data_raw = call i8* @realloc(i8* %old_data_raw, i64 %new_bytes)
  %new_data = ptrtoint i8* %new_data_raw to i64
  %is_null = icmp eq i64 %new_data, 0
  br i1 %is_null, label %store, label %update_cap

update_cap:
  store i64 %new_cap, i64* %cap_ptr
  store i64 %new_data, i64* %data_ptr_g
  br label %store

store:
  %ss2 = load i64, i64* @__gc_shadow_stack
  %data_addr_s = add i64 %ss2, 16
  %data_ptr_s = inttoptr i64 %data_addr_s to i64*
  %data_s = load i64, i64* %data_ptr_s
  ; Store root_addr at data[count]
  %offset = shl i64 %count, 3
  %slot_addr = add i64 %data_s, %offset
  %slot_ptr = inttoptr i64 %slot_addr to i64*
  store i64 %root_addr, i64* %slot_ptr
  ; Increment count
  %count_ptr2 = inttoptr i64 %ss2 to i64*
  %new_count = add i64 %count, 1
  store i64 %new_count, i64* %count_ptr2
  ret void
}

; Pop N roots from shadow stack
define void @__gc_pop_roots(i64 %n) {
entry:
  %inited = load i64, i64* @__gc_shadow_stack_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %done, label %pop

pop:
  %ss = load i64, i64* @__gc_shadow_stack
  %count_ptr = inttoptr i64 %ss to i64*
  %count = load i64, i64* %count_ptr
  %new_count = sub i64 %count, %n
  ; Clamp to 0
  %is_neg = icmp slt i64 %new_count, 0
  %final = select i1 %is_neg, i64 0, i64 %new_count
  store i64 %final, i64* %count_ptr
  br label %done

done:
  ret void
}

; Get current shadow stack depth
define i64 @__gc_shadow_stack_depth() {
entry:
  %inited = load i64, i64* @__gc_shadow_stack_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %zero, label %get

get:
  %ss = load i64, i64* @__gc_shadow_stack
  %count_ptr = inttoptr i64 %ss to i64*
  %count = load i64, i64* %count_ptr
  ret i64 %count

zero:
  ret i64 0
}

; =============================================================================
; GC Allocation
; =============================================================================

; Allocate a GC-tracked object. Returns user pointer (header is before it).
; With generational GC enabled, small objects go to the nursery first.
define i64 @__gc_alloc(i64 %size, i64 %type_tag) {
entry:
  ; Try nursery allocation if GC is enabled and nursery is initialized
  %enabled = load i64, i64* @__gc_enabled
  %is_enabled = icmp ne i64 %enabled, 0
  br i1 %is_enabled, label %try_nursery, label %do_old_gen_alloc

try_nursery:
  %nursery_inited = load i64, i64* @__gc_nursery_inited
  %has_nursery = icmp ne i64 %nursery_inited, 0
  br i1 %has_nursery, label %nursery_alloc, label %check_threshold

nursery_alloc:
  ; total_needed = size + 24 (header)
  %nursery_need = add i64 %size, 24
  %n_ptr = load i64, i64* @__gc_nursery_ptr
  %n_end = load i64, i64* @__gc_nursery_end
  %n_new_ptr = add i64 %n_ptr, %nursery_need
  %n_fits = icmp ule i64 %n_new_ptr, %n_end
  br i1 %n_fits, label %bump_alloc, label %nursery_full

bump_alloc:
  ; Fast path: bump allocate in nursery
  store i64 %n_new_ptr, i64* @__gc_nursery_ptr
  ; Initialize header (same layout as old gen but NOT linked into gc_head list)
  ; header[0] = 0 (next_ptr unused for nursery objects until promotion)
  %n_next_ptr = inttoptr i64 %n_ptr to i64*
  store i64 0, i64* %n_next_ptr
  ; header[8] = pack_info(0, type_tag, size)
  %n_info = call i64 @__gc_pack_info(i64 0, i64 %type_tag, i64 %size)
  %n_info_addr = add i64 %n_ptr, 8
  %n_info_ptr = inttoptr i64 %n_info_addr to i64*
  store i64 %n_info, i64* %n_info_ptr
  ; header[16] = magic sentinel
  %n_res_addr = add i64 %n_ptr, 16
  %n_res_ptr = inttoptr i64 %n_res_addr to i64*
  store i64 6557403441622859503, i64* %n_res_ptr
  ; Update stats
  %ac_n = load i64, i64* @__gc_alloc_count
  %ac_n_new = add i64 %ac_n, 1
  store i64 %ac_n_new, i64* @__gc_alloc_count
  %tb_n = load i64, i64* @__gc_total_bytes
  %tb_n_new = add i64 %tb_n, %nursery_need
  store i64 %tb_n_new, i64* @__gc_total_bytes
  ; Return user pointer = raw + 24
  %n_user = add i64 %n_ptr, 24
  ret i64 %n_user

nursery_full:
  ; Nursery is full — run minor GC to promote survivors, then retry
  call void @__gc_minor_collect()
  ; Retry nursery allocation
  %n_ptr2 = load i64, i64* @__gc_nursery_ptr
  %n_new_ptr2 = add i64 %n_ptr2, %nursery_need
  %n_fits2 = icmp ule i64 %n_new_ptr2, %n_end
  br i1 %n_fits2, label %bump_alloc_retry, label %check_threshold

bump_alloc_retry:
  ; Second bump attempt after minor GC
  store i64 %n_new_ptr2, i64* @__gc_nursery_ptr
  %n2_next_ptr = inttoptr i64 %n_ptr2 to i64*
  store i64 0, i64* %n2_next_ptr
  %n2_info = call i64 @__gc_pack_info(i64 0, i64 %type_tag, i64 %size)
  %n2_info_addr = add i64 %n_ptr2, 8
  %n2_info_ptr = inttoptr i64 %n2_info_addr to i64*
  store i64 %n2_info, i64* %n2_info_ptr
  %n2_res_addr = add i64 %n_ptr2, 16
  %n2_res_ptr = inttoptr i64 %n2_res_addr to i64*
  store i64 6557403441622859503, i64* %n2_res_ptr
  %ac_n2 = load i64, i64* @__gc_alloc_count
  %ac_n2_new = add i64 %ac_n2, 1
  store i64 %ac_n2_new, i64* @__gc_alloc_count
  %tb_n2 = load i64, i64* @__gc_total_bytes
  %tb_n2_new = add i64 %tb_n2, %nursery_need
  store i64 %tb_n2_new, i64* @__gc_total_bytes
  %n2_user = add i64 %n_ptr2, 24
  ret i64 %n2_user

check_threshold:
  ; Object too large for nursery or nursery still full after minor GC —
  ; fall back to old-gen allocation with threshold check
  %total = load i64, i64* @__gc_total_bytes
  %thresh = load i64, i64* @__gc_threshold
  %over = icmp uge i64 %total, %thresh
  br i1 %over, label %collect, label %do_old_gen_alloc

collect:
  call void @__gc_collect()
  ; Grow threshold if still > 50%
  %total2 = load i64, i64* @__gc_total_bytes
  %thresh2 = load i64, i64* @__gc_threshold
  %half = lshr i64 %thresh2, 1
  %still_high = icmp ugt i64 %total2, %half
  br i1 %still_high, label %grow_thresh, label %do_old_gen_alloc

grow_thresh:
  %new_thresh = shl i64 %thresh2, 1
  store i64 %new_thresh, i64* @__gc_threshold
  br label %do_old_gen_alloc

do_old_gen_alloc:
  ; Old generation allocation (malloc-based, linked list tracked)
  %alloc_size = add i64 %size, 24
  %raw_ptr = call i8* @malloc(i64 %alloc_size)
  %raw = ptrtoint i8* %raw_ptr to i64
  %is_null = icmp eq i64 %raw, 0
  br i1 %is_null, label %fail, label %init_header

init_header:
  ; header[0] = old gc_head (next pointer)
  %old_head = load i64, i64* @__gc_head
  %next_ptr = inttoptr i64 %raw to i64*
  store i64 %old_head, i64* %next_ptr
  ; header[8] = pack_info(0, type_tag, size)
  %info = call i64 @__gc_pack_info(i64 0, i64 %type_tag, i64 %size)
  %info_addr = add i64 %raw, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  store i64 %info, i64* %info_ptr
  ; header[16] = magic sentinel (0x5AFFC0DEDEADBEEF = 6557403441622859503)
  %res_addr = add i64 %raw, 16
  %res_ptr = inttoptr i64 %res_addr to i64*
  store i64 6557403441622859503, i64* %res_ptr
  ; Update gc_head
  store i64 %raw, i64* @__gc_head
  ; Update alloc_count
  %ac = load i64, i64* @__gc_alloc_count
  %ac_new = add i64 %ac, 1
  store i64 %ac_new, i64* @__gc_alloc_count
  ; Update total_bytes
  %tb = load i64, i64* @__gc_total_bytes
  %tb_new = add i64 %tb, %alloc_size
  store i64 %tb_new, i64* @__gc_total_bytes
  ; Return user pointer = raw + 24
  %user = add i64 %raw, 24
  ret i64 %user

fail:
  ret i64 0
}

; Allocate directly in old gen — GC-tracked but never triggers collection.
; Use for runtime helpers (e.g. __str_split) where locals hold GC pointers
; but are not registered as roots in the shadow stack.
define i64 @__gc_alloc_safe(i64 %size, i64 %type_tag) {
entry:
  %alloc_size = add i64 %size, 24
  %raw_ptr = call i8* @malloc(i64 %alloc_size)
  %raw = ptrtoint i8* %raw_ptr to i64
  %is_null = icmp eq i64 %raw, 0
  br i1 %is_null, label %fail, label %init_header

init_header:
  ; header[0] = old gc_head (next pointer)
  %old_head = load i64, i64* @__gc_head
  %next_ptr = inttoptr i64 %raw to i64*
  store i64 %old_head, i64* %next_ptr
  ; header[8] = pack_info(0, type_tag, size)
  %info = call i64 @__gc_pack_info(i64 0, i64 %type_tag, i64 %size)
  %info_addr = add i64 %raw, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  store i64 %info, i64* %info_ptr
  ; header[16] = magic sentinel
  %res_addr = add i64 %raw, 16
  %res_ptr = inttoptr i64 %res_addr to i64*
  store i64 6557403441622859503, i64* %res_ptr
  ; Update gc_head
  store i64 %raw, i64* @__gc_head
  ; Update alloc_count
  %ac = load i64, i64* @__gc_alloc_count
  %ac_new = add i64 %ac, 1
  store i64 %ac_new, i64* @__gc_alloc_count
  ; Update total_bytes
  %tb = load i64, i64* @__gc_total_bytes
  %tb_new = add i64 %tb, %alloc_size
  store i64 %tb_new, i64* @__gc_total_bytes
  ; Return user pointer = raw + 24
  %user = add i64 %raw, 24
  ret i64 %user

fail:
  ret i64 0
}

; Allocate zeroed memory
define i64 @__gc_alloc_zeroed(i64 %size, i64 %type_tag) {
entry:
  %ptr = call i64 @__gc_alloc(i64 %size, i64 %type_tag)
  %is_null = icmp eq i64 %ptr, 0
  br i1 %is_null, label %done, label %zero_loop_init

zero_loop_init:
  br label %zero_loop

zero_loop:
  %i = phi i64 [0, %zero_loop_init], [%i_next, %zero_body]
  %cmp = icmp ult i64 %i, %size
  br i1 %cmp, label %zero_body, label %done

zero_body:
  %addr = add i64 %ptr, %i
  %byte_ptr = inttoptr i64 %addr to i8*
  store i8 0, i8* %byte_ptr
  %i_next = add i64 %i, 1
  br label %zero_loop

done:
  ret i64 %ptr
}

; Reallocate: allocate new, copy old data, return new (old will be swept)
define i64 @__gc_realloc(i64 %old_user_ptr, i64 %new_size, i64 %type_tag) {
entry:
  %new_ptr = call i64 @__gc_alloc(i64 %new_size, i64 %type_tag)
  %new_null = icmp eq i64 %new_ptr, 0
  br i1 %new_null, label %ret_old, label %check_old

check_old:
  %old_null = icmp eq i64 %old_user_ptr, 0
  br i1 %old_null, label %ret_new, label %copy

copy:
  ; Get old size from header
  %old_header = sub i64 %old_user_ptr, 24
  %old_info_addr = add i64 %old_header, 8
  %old_info_ptr = inttoptr i64 %old_info_addr to i64*
  %old_info = load i64, i64* %old_info_ptr
  %old_size = call i64 @__gc_info_size(i64 %old_info)
  ; copy_size = min(old_size, new_size)
  %use_old = icmp ult i64 %old_size, %new_size
  %copy_size = select i1 %use_old, i64 %old_size, i64 %new_size
  ; Copy byte by byte (for small buffers; TODO: use memcpy for large)
  br label %copy_loop

copy_loop:
  %ci = phi i64 [0, %copy], [%ci_next, %copy_body]
  %copy_done = icmp uge i64 %ci, %copy_size
  br i1 %copy_done, label %ret_new, label %copy_body

copy_body:
  %src_addr = add i64 %old_user_ptr, %ci
  %src_ptr = inttoptr i64 %src_addr to i8*
  %byte = load i8, i8* %src_ptr
  %dst_addr = add i64 %new_ptr, %ci
  %dst_ptr = inttoptr i64 %dst_addr to i8*
  store i8 %byte, i8* %dst_ptr
  %ci_next = add i64 %ci, 1
  br label %copy_loop

ret_new:
  ret i64 %new_ptr

ret_old:
  ret i64 %old_user_ptr
}

; =============================================================================
; Mark Phase — Iterative Worklist
; =============================================================================
;
; Instead of recursive marking (which overflows the C stack on deep object
; graphs), we use an explicit mark stack (worklist). __gc_mark_object just
; validates and pushes onto the worklist. __gc_mark_drain processes items
; iteratively, pushing children as it goes.

; Mark stack globals
@__gc_mark_stack = private global i64 0       ; pointer to i64 array
@__gc_mark_stack_count = private global i64 0
@__gc_mark_stack_cap = private global i64 0

; Initialize the mark stack (called lazily on first push)
define private void @__gc_mark_stack_init() {
entry:
  %cap = load i64, i64* @__gc_mark_stack_cap
  %already = icmp ne i64 %cap, 0
  br i1 %already, label %done, label %do_init

do_init:
  ; Start with 4096 entries (32KB)
  %init_cap = add i64 4096, 0
  %bytes = shl i64 %init_cap, 3
  %raw = call i8* @malloc(i64 %bytes)
  %ptr = ptrtoint i8* %raw to i64
  store i64 %ptr, i64* @__gc_mark_stack
  store i64 0, i64* @__gc_mark_stack_count
  store i64 %init_cap, i64* @__gc_mark_stack_cap
  br label %done

done:
  ret void
}

; Push a value onto the mark stack
define private void @__gc_mark_push(i64 %val) {
entry:
  %cap = load i64, i64* @__gc_mark_stack_cap
  %need_init = icmp eq i64 %cap, 0
  br i1 %need_init, label %init, label %check_grow

init:
  call void @__gc_mark_stack_init()
  br label %check_grow

check_grow:
  %count = load i64, i64* @__gc_mark_stack_count
  %cap2 = load i64, i64* @__gc_mark_stack_cap
  %full = icmp uge i64 %count, %cap2
  br i1 %full, label %grow, label %do_push

grow:
  %new_cap = shl i64 %cap2, 1
  %new_bytes = shl i64 %new_cap, 3
  %old_ptr = load i64, i64* @__gc_mark_stack
  %old_raw = inttoptr i64 %old_ptr to i8*
  %new_raw = call i8* @realloc(i8* %old_raw, i64 %new_bytes)
  %new_ptr = ptrtoint i8* %new_raw to i64
  %realloc_ok = icmp ne i64 %new_ptr, 0
  br i1 %realloc_ok, label %update_cap, label %do_push

update_cap:
  store i64 %new_ptr, i64* @__gc_mark_stack
  store i64 %new_cap, i64* @__gc_mark_stack_cap
  br label %do_push

do_push:
  %count2 = load i64, i64* @__gc_mark_stack_count
  %stack = load i64, i64* @__gc_mark_stack
  %offset = shl i64 %count2, 3
  %slot_addr = add i64 %stack, %offset
  %slot_ptr = inttoptr i64 %slot_addr to i64*
  store i64 %val, i64* %slot_ptr
  %new_count = add i64 %count2, 1
  store i64 %new_count, i64* @__gc_mark_stack_count
  ret void
}

; Check if a value is a valid GC heap pointer.
; Uses a two-stage filter:
;   1. Quick checks: zero, alignment, and heap bounds (no memory access needed)
;   2. Magic number verification at header + 16 (safe because within known bounds)
; Magic = 0x5AFF_C0DE_DEAD_BEEF
define private i64 @__gc_is_heap_ptr(i64 %val) {
entry:
  %is_zero = icmp eq i64 %val, 0
  br i1 %is_zero, label %no, label %check_align

check_align:
  ; All GC user pointers are 8-byte aligned (malloc guarantee + 24-byte header).
  ; Filter out non-pointer values (string lengths, counts, enum tags, etc.)
  %align_bits = and i64 %val, 7
  %not_aligned = icmp ne i64 %align_bits, 0
  br i1 %not_aligned, label %no, label %check_bounds

check_bounds:
  ; Reject anything below 4GB. On arm64 macOS/Darwin, all heap allocations
  ; from malloc are above 0x100000000. This eliminates integer values like
  ; counts, capacities, and enum tags that happen to be 8-byte aligned.
  ; Also prevents speculative loads from crashing on unmapped low memory.
  %too_low = icmp ult i64 %val, 4294967296   ; 0x100000000 = 4GB
  br i1 %too_low, label %no, label %check_high

check_high:
  ; Upper bound: reject tagged NaN-boxed values and kernel addresses
  %too_high = icmp ugt i64 %val, 281474976710655  ; 0x0000FFFFFFFFFFFF
  br i1 %too_high, label %no, label %check_magic

check_magic:
  ; header = val - 24. Check magic at header + 16.
  ; Safe to load because val is within our known allocation bounds.
  %header = sub i64 %val, 24
  %magic_addr = add i64 %header, 16
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  ; Magic constant: 0x5AFFC0DEDEADBEEF = 6557403441622859503
  %is_magic = icmp eq i64 %magic, 6557403441622859503
  br i1 %is_magic, label %yes, label %no

yes:
  ret i64 1

no:
  ret i64 0
}

; Mark a single object: validate, set mark bit, push onto worklist.
; Does NOT recurse — children are processed by __gc_mark_drain.
define void @__gc_mark_object(i64 %user_ptr) {
entry:
  %is_zero = icmp eq i64 %user_ptr, 0
  br i1 %is_zero, label %done, label %validate

validate:
  %is_heap = call i64 @__gc_is_heap_ptr(i64 %user_ptr)
  %not_heap = icmp eq i64 %is_heap, 0
  br i1 %not_heap, label %done, label %check_marked

check_marked:
  %header = sub i64 %user_ptr, 24
  %info_addr = add i64 %header, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  %info = load i64, i64* %info_ptr
  %mark = call i64 @__gc_info_mark(i64 %info)
  %already_marked = icmp ne i64 %mark, 0
  br i1 %already_marked, label %done, label %do_mark

do_mark:
  ; Set mark bit
  %marked_info = or i64 %info, 1
  store i64 %marked_info, i64* %info_ptr
  ; Push onto worklist for iterative processing
  call void @__gc_mark_push(i64 %user_ptr)
  br label %done

done:
  ret void
}

; Drain the mark worklist: pop objects and trace their children iteratively.
define private void @__gc_mark_drain() {
entry:
  br label %loop

loop:
  %count = load i64, i64* @__gc_mark_stack_count
  %empty = icmp eq i64 %count, 0
  br i1 %empty, label %done, label %pop

pop:
  ; Pop from top of stack
  %new_count = sub i64 %count, 1
  store i64 %new_count, i64* @__gc_mark_stack_count
  %stack = load i64, i64* @__gc_mark_stack
  %offset = shl i64 %new_count, 3
  %slot_addr = add i64 %stack, %offset
  %slot_ptr = inttoptr i64 %slot_addr to i64*
  %user_ptr = load i64, i64* %slot_ptr
  ; Read object info to dispatch by type tag
  %header = sub i64 %user_ptr, 24
  %info_addr = add i64 %header, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  %info = load i64, i64* %info_ptr
  %tag = call i64 @__gc_info_tag(i64 %info)
  %size = call i64 @__gc_info_size(i64 %info)
  switch i64 %tag, label %check_class_tag [
    i64 0, label %loop           ; raw - no inner ptrs
    i64 1, label %loop           ; string - no inner ptrs
    i64 2, label %trace_list
    i64 3, label %trace_map
    i64 4, label %trace_closure
    i64 5, label %trace_instance
    i64 6, label %trace_sb
    i64 7, label %loop           ; data array - scanned by parent list
    i64 8, label %loop           ; kv array - scanned by parent map
    i64 9, label %trace_array    ; env - scan all slots
  ]

check_class_tag:
  ; Tags >= 10 are per-class instance tags — scan all fields like tag 5
  %is_class_instance = icmp uge i64 %tag, 10
  br i1 %is_class_instance, label %trace_instance, label %loop

trace_list:
  ; List: { count@0, capacity@8, data_ptr@16 }
  %list_data_addr = add i64 %user_ptr, 16
  %list_data_ptr = inttoptr i64 %list_data_addr to i64*
  %list_data = load i64, i64* %list_data_ptr
  ; Mark the data array itself
  call void @__gc_mark_object(i64 %list_data)
  ; Scan list elements
  %list_count_ptr = inttoptr i64 %user_ptr to i64*
  %list_count = load i64, i64* %list_count_ptr
  br label %list_loop

list_loop:
  %li = phi i64 [0, %trace_list], [%li_next, %list_body]
  %list_done = icmp uge i64 %li, %list_count
  br i1 %list_done, label %loop, label %list_body

list_body:
  %elem_offset = shl i64 %li, 3
  %elem_addr = add i64 %list_data, %elem_offset
  %elem_ptr = inttoptr i64 %elem_addr to i64*
  %elem = load i64, i64* %elem_ptr
  call void @__gc_mark_object(i64 %elem)
  %li_next = add i64 %li, 1
  br label %list_loop

trace_map:
  ; Map: { count@0, capacity@8, keys_ptr@16, values_ptr@24 }
  %map_keys_addr = add i64 %user_ptr, 16
  %map_keys_ptr = inttoptr i64 %map_keys_addr to i64*
  %map_keys = load i64, i64* %map_keys_ptr
  %map_vals_addr = add i64 %user_ptr, 24
  %map_vals_ptr = inttoptr i64 %map_vals_addr to i64*
  %map_vals = load i64, i64* %map_vals_ptr
  call void @__gc_mark_object(i64 %map_keys)
  call void @__gc_mark_object(i64 %map_vals)
  ; Scan entries
  %map_count_ptr = inttoptr i64 %user_ptr to i64*
  %map_count = load i64, i64* %map_count_ptr
  br label %map_loop

map_loop:
  %mi = phi i64 [0, %trace_map], [%mi_next, %map_body]
  %map_done = icmp uge i64 %mi, %map_count
  br i1 %map_done, label %loop, label %map_body

map_body:
  %mk_offset = shl i64 %mi, 3
  %mk_addr = add i64 %map_keys, %mk_offset
  %mk_ptr = inttoptr i64 %mk_addr to i64*
  %mk = load i64, i64* %mk_ptr
  call void @__gc_mark_object(i64 %mk)
  %mv_addr = add i64 %map_vals, %mk_offset
  %mv_ptr = inttoptr i64 %mv_addr to i64*
  %mv = load i64, i64* %mv_ptr
  call void @__gc_mark_object(i64 %mv)
  %mi_next = add i64 %mi, 1
  br label %map_loop

trace_closure:
  ; Closure: { fn_ptr@0, env_ptr@8 }
  ; fn_ptr is code, skip. Trace env_ptr.
  %env_addr = add i64 %user_ptr, 8
  %env_ptr_c = inttoptr i64 %env_addr to i64*
  %env_val = load i64, i64* %env_ptr_c
  call void @__gc_mark_object(i64 %env_val)
  br label %loop

trace_instance:
  ; Class instance: N fields (size/8 slots), all scanned
  %inst_num_fields = lshr i64 %size, 3
  br label %inst_loop

inst_loop:
  %ii = phi i64 [0, %trace_instance], [%ii_next, %inst_body]
  %inst_done = icmp uge i64 %ii, %inst_num_fields
  br i1 %inst_done, label %loop, label %inst_body

inst_body:
  %field_offset = shl i64 %ii, 3
  %field_addr = add i64 %user_ptr, %field_offset
  %field_ptr = inttoptr i64 %field_addr to i64*
  %field_val = load i64, i64* %field_ptr
  call void @__gc_mark_object(i64 %field_val)
  %ii_next = add i64 %ii, 1
  br label %inst_loop

trace_sb:
  ; StringBuilder: { len@0, cap@8, buf_ptr@16 }
  %sb_buf_addr = add i64 %user_ptr, 16
  %sb_buf_ptr = inttoptr i64 %sb_buf_addr to i64*
  %sb_buf = load i64, i64* %sb_buf_ptr
  call void @__gc_mark_object(i64 %sb_buf)
  br label %loop

trace_array:
  ; Data/KV/Env arrays: flat array of i64, scan all
  %arr_num = lshr i64 %size, 3
  br label %arr_loop

arr_loop:
  %ai = phi i64 [0, %trace_array], [%ai_next, %arr_body]
  %arr_done = icmp uge i64 %ai, %arr_num
  br i1 %arr_done, label %loop, label %arr_body

arr_body:
  %arr_offset = shl i64 %ai, 3
  %arr_elem_addr = add i64 %user_ptr, %arr_offset
  %arr_elem_ptr = inttoptr i64 %arr_elem_addr to i64*
  %arr_elem = load i64, i64* %arr_elem_ptr
  call void @__gc_mark_object(i64 %arr_elem)
  %ai_next = add i64 %ai, 1
  br label %arr_loop

done:
  ret void
}

; Mark phase: scan all roots from shadow stack, then drain the worklist
define private void @__gc_mark() {
entry:
  ; Reset mark stack count (reuse existing allocation)
  store i64 0, i64* @__gc_mark_stack_count
  %inited = load i64, i64* @__gc_shadow_stack_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %drain, label %scan

scan:
  %ss = load i64, i64* @__gc_shadow_stack
  %count_ptr = inttoptr i64 %ss to i64*
  %count = load i64, i64* %count_ptr
  %data_addr = add i64 %ss, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  %data = load i64, i64* %data_ptr
  br label %loop

loop:
  %i = phi i64 [0, %scan], [%i_next, %next]
  %loop_done = icmp uge i64 %i, %count
  br i1 %loop_done, label %drain, label %loop_body

loop_body:
  ; Each entry is the address of a variable. Read the value at that address.
  %slot_offset = shl i64 %i, 3
  %slot_addr = add i64 %data, %slot_offset
  %slot_ptr = inttoptr i64 %slot_addr to i64*
  %root_addr = load i64, i64* %slot_ptr
  %root_null = icmp eq i64 %root_addr, 0
  br i1 %root_null, label %next, label %deref

deref:
  %val_ptr = inttoptr i64 %root_addr to i64*
  %val = load i64, i64* %val_ptr
  call void @__gc_mark_object(i64 %val)
  br label %next

next:
  %i_next = add i64 %i, 1
  br label %loop

drain:
  ; After all roots are pushed, iteratively process the worklist
  call void @__gc_mark_drain()
  br label %done

done:
  ret void
}

; =============================================================================
; Sweep Phase
; =============================================================================

; Walk the allocation list, free unmarked objects, clear marks on survivors
define private void @__gc_sweep_impl() {
entry:
  %prev_alloca = alloca i64
  %curr_alloca = alloca i64
  store i64 0, i64* %prev_alloca
  %head = load i64, i64* @__gc_head
  store i64 %head, i64* %curr_alloca
  br label %loop

loop:
  %current = load i64, i64* %curr_alloca
  %is_end = icmp eq i64 %current, 0
  br i1 %is_end, label %done, label %process

process:
  ; Read next pointer (at offset 0 of header)
  %next_ptr = inttoptr i64 %current to i64*
  %next = load i64, i64* %next_ptr
  ; Read info (at offset 8)
  %info_addr = add i64 %current, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  %info = load i64, i64* %info_ptr
  ; Check mark bit
  %mark = call i64 @__gc_info_mark(i64 %info)
  %is_unmarked = icmp eq i64 %mark, 0
  br i1 %is_unmarked, label %free_node, label %keep_node

free_node:
  ; Unlink from list
  %prev = load i64, i64* %prev_alloca
  %has_prev = icmp ne i64 %prev, 0
  br i1 %has_prev, label %unlink_mid, label %unlink_head

unlink_head:
  store i64 %next, i64* @__gc_head
  br label %do_free

unlink_mid:
  %prev_next_ptr = inttoptr i64 %prev to i64*
  store i64 %next, i64* %prev_next_ptr
  br label %do_free

do_free:
  ; Update stats
  %size = call i64 @__gc_info_size(i64 %info)
  %total_free = add i64 %size, 24
  %ac = load i64, i64* @__gc_alloc_count
  %ac_new = sub i64 %ac, 1
  store i64 %ac_new, i64* @__gc_alloc_count
  %tb = load i64, i64* @__gc_total_bytes
  %tb_new = sub i64 %tb, %total_free
  store i64 %tb_new, i64* @__gc_total_bytes
  %fb = load i64, i64* @__gc_freed_bytes
  %fb_new = add i64 %fb, %total_free
  store i64 %fb_new, i64* @__gc_freed_bytes
  ; Free the memory
  %free_ptr = inttoptr i64 %current to i8*
  call void @free(i8* %free_ptr)
  ; Advance (prev stays the same)
  store i64 %next, i64* %curr_alloca
  br label %loop

keep_node:
  ; Clear mark bit for next cycle
  %cleared_info = and i64 %info, -2   ; clear bit 0
  store i64 %cleared_info, i64* %info_ptr
  ; Advance
  store i64 %current, i64* %prev_alloca
  store i64 %next, i64* %curr_alloca
  br label %loop

done:
  ret void
}

; =============================================================================
; Public API
; =============================================================================

; Run a full mark-and-sweep collection
define void @__gc_collect() {
entry:
  call void @__gc_mark()
  call void @__gc_sweep_impl()
  %c = load i64, i64* @__gc_collections
  %c_new = add i64 %c, 1
  store i64 %c_new, i64* @__gc_collections
  ret void
}

; Enable automatic GC (also initializes nursery for generational collection)
define void @__gc_enable() {
entry:
  store i64 1, i64* @__gc_enabled
  ; Set default threshold if not already set
  %thresh = load i64, i64* @__gc_threshold
  %is_zero = icmp eq i64 %thresh, 0
  br i1 %is_zero, label %set_default, label %init_ss

set_default:
  store i64 65536, i64* @__gc_threshold
  br label %init_ss

init_ss:
  call void @__gc_init_shadow_stack()
  call void @__gc_nursery_init()
  ret void
}

; Disable automatic GC
define void @__gc_disable() {
entry:
  store i64 0, i64* @__gc_enabled
  ret void
}

; Set the collection threshold
define void @__gc_set_threshold(i64 %bytes) {
entry:
  store i64 %bytes, i64* @__gc_threshold
  ret void
}

; Statistics accessors
define i64 @__gc_stat_alloc_count() {
entry:
  %v = load i64, i64* @__gc_alloc_count
  ret i64 %v
}

define i64 @__gc_stat_total_bytes() {
entry:
  %v = load i64, i64* @__gc_total_bytes
  ret i64 %v
}

define i64 @__gc_stat_collections() {
entry:
  %v = load i64, i64* @__gc_collections
  ret i64 %v
}

define i64 @__gc_stat_freed_bytes() {
entry:
  %v = load i64, i64* @__gc_freed_bytes
  ret i64 %v
}

define i64 @__gc_stat_threshold() {
entry:
  %v = load i64, i64* @__gc_threshold
  ret i64 %v
}

; Print GC statistics (minimal: writes "GC: ok\n" to stderr)
define void @__gc_debug_stats() {
entry:
  %buf = alloca [7 x i8]
  %p = getelementptr [7 x i8], [7 x i8]* %buf, i64 0, i64 0
  store i8 71, i8* %p
  %p1 = getelementptr i8, i8* %p, i64 1
  store i8 67, i8* %p1
  %p2 = getelementptr i8, i8* %p, i64 2
  store i8 58, i8* %p2
  %p3 = getelementptr i8, i8* %p, i64 3
  store i8 32, i8* %p3
  %p4 = getelementptr i8, i8* %p, i64 4
  store i8 111, i8* %p4
  %p5 = getelementptr i8, i8* %p, i64 5
  store i8 107, i8* %p5
  %p6 = getelementptr i8, i8* %p, i64 6
  store i8 10, i8* %p6
  call i64 @write(i32 2, i8* %p, i64 7)
  ret void
}

; =============================================================================
; GC-Aware Allocation Wrappers
; These are drop-in replacements that use GC tracking.
; =============================================================================

; Allocate a new GC-tracked list
; NOTE: Temporarily disables GC to prevent use-after-free during multi-alloc.
define i64 @__gc_list_new() {
entry:
  %saved_enabled = load i64, i64* @__gc_enabled
  store i64 0, i64* @__gc_enabled
  %list = call i64 @__gc_alloc(i64 24, i64 2)
  %is_null = icmp eq i64 %list, 0
  br i1 %is_null, label %done, label %init

init:
  %count_ptr = inttoptr i64 %list to i64*
  store i64 0, i64* %count_ptr
  %cap_addr = add i64 %list, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 8, i64* %cap_ptr
  %data = call i64 @__gc_alloc_zeroed(i64 64, i64 7)
  %data_addr = add i64 %list, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  store i64 %data, i64* %data_ptr
  br label %done

done:
  store i64 %saved_enabled, i64* @__gc_enabled
  ret i64 %list
}

; Allocate a new GC-tracked map
; NOTE: Temporarily disables GC to prevent use-after-free during multi-alloc.
define i64 @__gc_map_new() {
entry:
  %saved_enabled = load i64, i64* @__gc_enabled
  store i64 0, i64* @__gc_enabled
  %map = call i64 @__gc_alloc(i64 32, i64 3)
  %is_null = icmp eq i64 %map, 0
  br i1 %is_null, label %done, label %init

init:
  %count_ptr = inttoptr i64 %map to i64*
  store i64 0, i64* %count_ptr
  %cap_addr = add i64 %map, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 16, i64* %cap_ptr
  %keys = call i64 @__gc_alloc_zeroed(i64 128, i64 8)
  %keys_addr = add i64 %map, 16
  %keys_ptr = inttoptr i64 %keys_addr to i64*
  store i64 %keys, i64* %keys_ptr
  %vals = call i64 @__gc_alloc_zeroed(i64 128, i64 8)
  %vals_addr = add i64 %map, 24
  %vals_ptr = inttoptr i64 %vals_addr to i64*
  store i64 %vals, i64* %vals_ptr
  br label %done

done:
  store i64 %saved_enabled, i64* @__gc_enabled
  ret i64 %map
}

; Allocate a new GC-tracked StringBuilder
; NOTE: Temporarily disables GC to prevent use-after-free during multi-alloc.
define i64 @__gc_stringbuilder_new() {
entry:
  %saved_enabled = load i64, i64* @__gc_enabled
  store i64 0, i64* @__gc_enabled
  %sb = call i64 @__gc_alloc(i64 24, i64 6)
  %is_null = icmp eq i64 %sb, 0
  br i1 %is_null, label %done, label %init

init:
  ; len = 0
  %len_ptr = inttoptr i64 %sb to i64*
  store i64 0, i64* %len_ptr
  ; cap = 1024
  %cap_addr = add i64 %sb, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 1024, i64* %cap_ptr
  ; buf = gc_alloc(1024, tag=1)
  %buf = call i64 @__gc_alloc(i64 1024, i64 1)
  ; null-terminate
  %buf_byte = inttoptr i64 %buf to i8*
  store i8 0, i8* %buf_byte
  %buf_addr = add i64 %sb, 16
  %buf_ptr = inttoptr i64 %buf_addr to i64*
  store i64 %buf, i64* %buf_ptr
  br label %done

done:
  store i64 %saved_enabled, i64* @__gc_enabled
  ret i64 %sb
}

; Allocate a GC-tracked string buffer
define i64 @__gc_string_alloc(i64 %size) {
entry:
  %ptr = call i64 @__gc_alloc(i64 %size, i64 1)
  ret i64 %ptr
}

; Allocate a GC-tracked closure pair
define i64 @__gc_closure_new(i64 %fn_ptr, i64 %env_ptr) {
entry:
  %raw = call i64 @__gc_alloc(i64 16, i64 4)
  %is_null = icmp eq i64 %raw, 0
  br i1 %is_null, label %done, label %init

init:
  %fn_slot = inttoptr i64 %raw to i64*
  store i64 %fn_ptr, i64* %fn_slot
  %env_addr = add i64 %raw, 8
  %env_slot = inttoptr i64 %env_addr to i64*
  store i64 %env_ptr, i64* %env_slot
  br label %done

done:
  ret i64 %raw
}

; Allocate a GC-tracked closure environment
define i64 @__gc_env_alloc(i64 %num_slots) {
entry:
  %size = shl i64 %num_slots, 3
  %ptr = call i64 @__gc_alloc_zeroed(i64 %size, i64 9)
  ret i64 %ptr
}

; Allocate a GC-tracked class instance
define i64 @__gc_instance_alloc(i64 %num_fields) {
entry:
  %size = shl i64 %num_fields, 3
  %ptr = call i64 @__gc_alloc_zeroed(i64 %size, i64 5)
  ret i64 %ptr
}

; Get the GC type tag for a heap-allocated object.
; Returns the type tag from the GC header (0 if not a GC object).
; Tags 0-9 are built-in types, tags >= 10 are per-class instance tags.
define i64 @__gc_get_type_tag(i64 %ptr) {
entry:
  %is_null = icmp eq i64 %ptr, 0
  br i1 %is_null, label %ret_zero, label %read_header

read_header:
  ; Header is 24 bytes before the user pointer: header_addr = ptr - 24
  %header_addr = sub i64 %ptr, 24
  ; info field is at header + 8
  %info_addr = add i64 %header_addr, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  %info = load i64, i64* %info_ptr
  ; Extract type tag from info (bits 8-15)
  %tag = call i64 @__gc_info_tag(i64 %info)
  ret i64 %tag

ret_zero:
  ret i64 0
}

; =============================================================================
; GC-Aware list push (handles realloc with GC tracking)
; =============================================================================

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
  %new_data = call i64 @__gc_realloc(i64 %old_data, i64 %new_bytes, i64 7)
  store i64 %new_cap, i64* %cap_ptr
  store i64 %new_data, i64* %data_ptr_g
  br label %store

store:
  %data_addr_s = add i64 %list, 16
  %data_ptr_s = inttoptr i64 %data_addr_s to i64*
  %data = load i64, i64* %data_ptr_s
  %count2 = load i64, i64* %count_ptr
  %offset = shl i64 %count2, 3
  %slot = add i64 %data, %offset
  %slot_ptr = inttoptr i64 %slot to i64*
  store i64 %value, i64* %slot_ptr
  %new_count = add i64 %count2, 1
  store i64 %new_count, i64* %count_ptr
  br label %done

done:
  ret void
}

; =============================================================================
; Generational GC — Nursery Implementation
; =============================================================================

; Initialize the nursery arena
define void @__gc_nursery_init() {
entry:
  %already = load i64, i64* @__gc_nursery_inited
  %is_inited = icmp ne i64 %already, 0
  br i1 %is_inited, label %done, label %do_init

do_init:
  %nsize = load i64, i64* @__gc_nursery_size
  %arena_raw = call i8* @malloc(i64 %nsize)
  %arena = ptrtoint i8* %arena_raw to i64
  %is_null = icmp eq i64 %arena, 0
  br i1 %is_null, label %done, label %store_arena

store_arena:
  store i64 %arena, i64* @__gc_nursery_start
  store i64 %arena, i64* @__gc_nursery_ptr
  %end_addr = add i64 %arena, %nsize
  store i64 %end_addr, i64* @__gc_nursery_end
  store i64 1, i64* @__gc_nursery_inited
  ; Initialize remembered set
  call void @__gc_remembered_set_init()
  br label %done

done:
  ret void
}

; Initialize the remembered set
define private void @__gc_remembered_set_init() {
entry:
  %already = load i64, i64* @__gc_remembered_set_inited
  %is_inited = icmp ne i64 %already, 0
  br i1 %is_inited, label %done, label %do_init

do_init:
  ; Allocate struct: { count: i64, capacity: i64, data_ptr: i64 }
  %rs_raw = call i8* @malloc(i64 24)
  %rs = ptrtoint i8* %rs_raw to i64
  %is_null = icmp eq i64 %rs, 0
  br i1 %is_null, label %done, label %init_struct

init_struct:
  ; count = 0
  %count_ptr = inttoptr i64 %rs to i64*
  store i64 0, i64* %count_ptr
  ; capacity = 256
  %cap_addr = add i64 %rs, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  store i64 256, i64* %cap_ptr
  ; data = malloc(256 * 8)
  %data_raw = call i8* @malloc(i64 2048)
  %data = ptrtoint i8* %data_raw to i64
  %data_addr = add i64 %rs, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  store i64 %data, i64* %data_ptr
  store i64 %rs, i64* @__gc_remembered_set
  store i64 1, i64* @__gc_remembered_set_inited
  br label %done

done:
  ret void
}

; Check if a user pointer is in the nursery
define i64 @__gc_is_nursery_ptr(i64 %ptr) {
entry:
  %is_zero = icmp eq i64 %ptr, 0
  br i1 %is_zero, label %no, label %check_inited

check_inited:
  %inited = load i64, i64* @__gc_nursery_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %no, label %check_range

check_range:
  ; A user pointer is in nursery if its header (ptr - 24) is within [start, end)
  %header = sub i64 %ptr, 24
  %start = load i64, i64* @__gc_nursery_start
  %end_val = load i64, i64* @__gc_nursery_end
  %above_start = icmp uge i64 %header, %start
  %below_end = icmp ult i64 %header, %end_val
  %in_range = and i1 %above_start, %below_end
  br i1 %in_range, label %yes, label %no

yes:
  ret i64 1

no:
  ret i64 0
}

; Write barrier: call when storing a pointer into an old-gen slot.
; Records old-gen slots that reference nursery objects.
define void @__gc_write_barrier(i64 %slot_addr, i64 %new_value) {
entry:
  %inited = load i64, i64* @__gc_nursery_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %done, label %check_value

check_value:
  ; Is the new value a nursery pointer?
  %val_in_nursery = call i64 @__gc_is_nursery_ptr(i64 %new_value)
  %not_nursery_val = icmp eq i64 %val_in_nursery, 0
  br i1 %not_nursery_val, label %done, label %check_slot

check_slot:
  ; Is the slot in old-gen (NOT in nursery)?
  %start = load i64, i64* @__gc_nursery_start
  %end_val = load i64, i64* @__gc_nursery_end
  %slot_above = icmp uge i64 %slot_addr, %start
  %slot_below = icmp ult i64 %slot_addr, %end_val
  %slot_in_nursery = and i1 %slot_above, %slot_below
  br i1 %slot_in_nursery, label %done, label %record

record:
  %rs_inited = load i64, i64* @__gc_remembered_set_inited
  %rs_not_inited = icmp eq i64 %rs_inited, 0
  br i1 %rs_not_inited, label %done, label %do_record

do_record:
  %rs = load i64, i64* @__gc_remembered_set
  %count_ptr = inttoptr i64 %rs to i64*
  %count = load i64, i64* %count_ptr
  %cap_addr = add i64 %rs, 8
  %cap_ptr = inttoptr i64 %cap_addr to i64*
  %cap = load i64, i64* %cap_ptr
  %need_grow = icmp uge i64 %count, %cap
  br i1 %need_grow, label %grow, label %store_entry

grow:
  %new_cap = shl i64 %cap, 1
  %new_bytes = shl i64 %new_cap, 3
  %data_addr_g = add i64 %rs, 16
  %data_ptr_g = inttoptr i64 %data_addr_g to i64*
  %old_data = load i64, i64* %data_ptr_g
  %old_data_raw = inttoptr i64 %old_data to i8*
  %new_data_raw = call i8* @realloc(i8* %old_data_raw, i64 %new_bytes)
  %new_data = ptrtoint i8* %new_data_raw to i64
  %realloc_failed = icmp eq i64 %new_data, 0
  br i1 %realloc_failed, label %done, label %update_cap

update_cap:
  store i64 %new_cap, i64* %cap_ptr
  store i64 %new_data, i64* %data_ptr_g
  br label %store_entry

store_entry:
  %rs2 = load i64, i64* @__gc_remembered_set
  %data_addr_s = add i64 %rs2, 16
  %data_ptr_s = inttoptr i64 %data_addr_s to i64*
  %data_s = load i64, i64* %data_ptr_s
  %offset = shl i64 %count, 3
  %entry_addr = add i64 %data_s, %offset
  %entry_ptr = inttoptr i64 %entry_addr to i64*
  store i64 %slot_addr, i64* %entry_ptr
  %new_count = add i64 %count, 1
  %count_ptr2 = inttoptr i64 %rs2 to i64*
  store i64 %new_count, i64* %count_ptr2
  br label %done

done:
  ret void
}

; =============================================================================
; Minor GC — Promote live nursery objects to old generation
; =============================================================================

define void @__gc_minor_collect() {
entry:
  %inited = load i64, i64* @__gc_nursery_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %done, label %begin

begin:
  ; Phase 1: Mark nursery objects reachable from roots
  call void @__gc_minor_mark_roots()
  ; Phase 2: Promote marked nursery objects, install forwarding pointers
  call void @__gc_minor_promote()
  ; Phase 3: Update all references to point to new old-gen locations
  call void @__gc_minor_update_refs()
  ; Phase 4: Reset nursery bump pointer
  %start = load i64, i64* @__gc_nursery_start
  store i64 %start, i64* @__gc_nursery_ptr
  ; Clear remembered set
  call void @__gc_remembered_set_clear()
  ; Update minor collection count
  %mc = load i64, i64* @__gc_minor_collections
  %mc_new = add i64 %mc, 1
  store i64 %mc_new, i64* @__gc_minor_collections
  br label %done

done:
  ret void
}

; Mark nursery objects reachable from shadow stack and remembered set
define private void @__gc_minor_mark_roots() optnone noinline {
entry:
  %ss_inited = load i64, i64* @__gc_shadow_stack_inited
  %not_inited = icmp eq i64 %ss_inited, 0
  br i1 %not_inited, label %scan_remembered, label %scan_roots

scan_roots:
  %ss = load i64, i64* @__gc_shadow_stack
  %count_ptr = inttoptr i64 %ss to i64*
  %count = load i64, i64* %count_ptr
  %data_addr = add i64 %ss, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  %data = load i64, i64* %data_ptr
  br label %root_loop

root_loop:
  %ri = phi i64 [0, %scan_roots], [%ri_next, %root_next]
  %root_done = icmp uge i64 %ri, %count
  br i1 %root_done, label %scan_remembered, label %root_body

root_body:
  %slot_offset = shl i64 %ri, 3
  %slot_addr = add i64 %data, %slot_offset
  %slot_ptr = inttoptr i64 %slot_addr to i64*
  %root_addr = load i64, i64* %slot_ptr
  %root_null = icmp eq i64 %root_addr, 0
  br i1 %root_null, label %root_next, label %root_deref

root_deref:
  %val_ptr = inttoptr i64 %root_addr to i64*
  %val = load i64, i64* %val_ptr
  call void @__gc_minor_mark_value(i64 %val)
  br label %root_next

root_next:
  %ri_next = add i64 %ri, 1
  br label %root_loop

scan_remembered:
  %rs_inited = load i64, i64* @__gc_remembered_set_inited
  %rs_not_inited = icmp eq i64 %rs_inited, 0
  br i1 %rs_not_inited, label %done, label %do_scan_rs

do_scan_rs:
  %rs = load i64, i64* @__gc_remembered_set
  %rs_count_ptr = inttoptr i64 %rs to i64*
  %rs_count = load i64, i64* %rs_count_ptr
  %rs_data_addr = add i64 %rs, 16
  %rs_data_ptr = inttoptr i64 %rs_data_addr to i64*
  %rs_data = load i64, i64* %rs_data_ptr
  br label %rs_loop

rs_loop:
  %rsi = phi i64 [0, %do_scan_rs], [%rsi_next, %rs_next]
  %rs_done = icmp uge i64 %rsi, %rs_count
  br i1 %rs_done, label %done, label %rs_body

rs_body:
  %rs_offset = shl i64 %rsi, 3
  %rs_entry_addr = add i64 %rs_data, %rs_offset
  %rs_entry_ptr = inttoptr i64 %rs_entry_addr to i64*
  %rs_slot = load i64, i64* %rs_entry_ptr
  %rs_val_ptr = inttoptr i64 %rs_slot to i64*
  %rs_val = load i64, i64* %rs_val_ptr
  call void @__gc_minor_mark_value(i64 %rs_val)
  br label %rs_next

rs_next:
  %rsi_next = add i64 %rsi, 1
  br label %rs_loop

done:
  ret void
}

; Mark a nursery object as live (recursive for children)
define private void @__gc_minor_mark_value(i64 %val) optnone noinline {
entry:
  %in_nursery = call i64 @__gc_is_nursery_ptr(i64 %val)
  %not_nursery = icmp eq i64 %in_nursery, 0
  br i1 %not_nursery, label %done, label %check_marked

check_marked:
  %header = sub i64 %val, 24
  %info_addr = add i64 %header, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  %info = load i64, i64* %info_ptr
  %mark = call i64 @__gc_info_mark(i64 %info)
  %already = icmp ne i64 %mark, 0
  br i1 %already, label %done, label %do_mark

do_mark:
  %marked_info = or i64 %info, 1
  store i64 %marked_info, i64* %info_ptr
  ; Trace children
  %tag = call i64 @__gc_info_tag(i64 %info)
  %size = call i64 @__gc_info_size(i64 %info)
  switch i64 %tag, label %check_class_tag [
    i64 0, label %done
    i64 1, label %done
    i64 2, label %trace_list
    i64 3, label %trace_map
    i64 4, label %trace_closure
    i64 5, label %trace_instance
    i64 6, label %trace_sb
    i64 7, label %done
    i64 8, label %done
    i64 9, label %trace_env
  ]

check_class_tag:
  ; Tags >= 10 are per-class instance tags — scan all fields like tag 5
  %is_class_inst = icmp uge i64 %tag, 10
  br i1 %is_class_inst, label %trace_instance, label %done

trace_list:
  %list_data_addr = add i64 %val, 16
  %list_data_ptr = inttoptr i64 %list_data_addr to i64*
  %list_data = load i64, i64* %list_data_ptr
  call void @__gc_minor_mark_value(i64 %list_data)
  %list_count_ptr = inttoptr i64 %val to i64*
  %list_count = load i64, i64* %list_count_ptr
  br label %list_loop

list_loop:
  %li = phi i64 [0, %trace_list], [%li_next, %list_body]
  %list_done = icmp uge i64 %li, %list_count
  br i1 %list_done, label %done, label %list_body

list_body:
  %elem_offset = shl i64 %li, 3
  %elem_addr = add i64 %list_data, %elem_offset
  %elem_ptr = inttoptr i64 %elem_addr to i64*
  %elem = load i64, i64* %elem_ptr
  call void @__gc_minor_mark_value(i64 %elem)
  %li_next = add i64 %li, 1
  br label %list_loop

trace_map:
  %map_keys_addr = add i64 %val, 16
  %map_keys_ptr = inttoptr i64 %map_keys_addr to i64*
  %map_keys = load i64, i64* %map_keys_ptr
  %map_vals_addr = add i64 %val, 24
  %map_vals_ptr = inttoptr i64 %map_vals_addr to i64*
  %map_vals = load i64, i64* %map_vals_ptr
  call void @__gc_minor_mark_value(i64 %map_keys)
  call void @__gc_minor_mark_value(i64 %map_vals)
  %map_count_ptr = inttoptr i64 %val to i64*
  %map_count = load i64, i64* %map_count_ptr
  br label %map_loop

map_loop:
  %mi = phi i64 [0, %trace_map], [%mi_next, %map_body]
  %map_done = icmp uge i64 %mi, %map_count
  br i1 %map_done, label %done, label %map_body

map_body:
  %mk_offset = shl i64 %mi, 3
  %mk_addr = add i64 %map_keys, %mk_offset
  %mk_ptr = inttoptr i64 %mk_addr to i64*
  %mk = load i64, i64* %mk_ptr
  call void @__gc_minor_mark_value(i64 %mk)
  %mv_addr = add i64 %map_vals, %mk_offset
  %mv_ptr = inttoptr i64 %mv_addr to i64*
  %mv = load i64, i64* %mv_ptr
  call void @__gc_minor_mark_value(i64 %mv)
  %mi_next = add i64 %mi, 1
  br label %map_loop

trace_closure:
  %env_addr = add i64 %val, 8
  %env_ptr_c = inttoptr i64 %env_addr to i64*
  %env_val = load i64, i64* %env_ptr_c
  call void @__gc_minor_mark_value(i64 %env_val)
  br label %done

trace_instance:
  %inst_num_fields = lshr i64 %size, 3
  br label %inst_loop

inst_loop:
  %ii = phi i64 [0, %trace_instance], [%ii_next, %inst_body]
  %inst_done = icmp uge i64 %ii, %inst_num_fields
  br i1 %inst_done, label %done, label %inst_body

inst_body:
  %field_offset = shl i64 %ii, 3
  %field_addr = add i64 %val, %field_offset
  %field_ptr = inttoptr i64 %field_addr to i64*
  %field_val = load i64, i64* %field_ptr
  call void @__gc_minor_mark_value(i64 %field_val)
  %ii_next = add i64 %ii, 1
  br label %inst_loop

trace_sb:
  %sb_buf_addr = add i64 %val, 16
  %sb_buf_ptr = inttoptr i64 %sb_buf_addr to i64*
  %sb_buf = load i64, i64* %sb_buf_ptr
  call void @__gc_minor_mark_value(i64 %sb_buf)
  br label %done

trace_env:
  %env_num = lshr i64 %size, 3
  br label %env_loop

env_loop:
  %ei = phi i64 [0, %trace_env], [%ei_next, %env_body]
  %env_done = icmp uge i64 %ei, %env_num
  br i1 %env_done, label %done, label %env_body

env_body:
  %env_offset = shl i64 %ei, 3
  %env_elem_addr = add i64 %val, %env_offset
  %env_elem_ptr = inttoptr i64 %env_elem_addr to i64*
  %env_elem = load i64, i64* %env_elem_ptr
  call void @__gc_minor_mark_value(i64 %env_elem)
  %ei_next = add i64 %ei, 1
  br label %env_loop

done:
  ret void
}

; Promote marked nursery objects to old gen, install forwarding pointers.
; Forwarding sentinel in header[16]: 0x5AFF_F0AD_F0AD_F0AD = 6557438972390461613
define private void @__gc_minor_promote() optnone noinline {
entry:
  %start = load i64, i64* @__gc_nursery_start
  %end_val = load i64, i64* @__gc_nursery_ptr
  br label %walk_loop

walk_loop:
  %pos = phi i64 [%start, %entry], [%next_pos, %advance]
  %at_end = icmp uge i64 %pos, %end_val
  br i1 %at_end, label %done, label %read_obj

read_obj:
  %info_addr = add i64 %pos, 8
  %info_ptr = inttoptr i64 %info_addr to i64*
  %info = load i64, i64* %info_ptr
  %size = call i64 @__gc_info_size(i64 %info)
  %obj_total = add i64 %size, 24
  %mark = call i64 @__gc_info_mark(i64 %info)
  %is_live = icmp ne i64 %mark, 0
  br i1 %is_live, label %promote, label %advance

promote:
  ; Allocate directly in old gen (bypass nursery)
  %old_alloc_size = add i64 %size, 24
  %old_raw_ptr = call i8* @malloc(i64 %old_alloc_size)
  %old_raw = ptrtoint i8* %old_raw_ptr to i64
  %is_null = icmp eq i64 %old_raw, 0
  br i1 %is_null, label %advance, label %do_promote

do_promote:
  ; Link into old-gen list
  %old_head = load i64, i64* @__gc_head
  %old_next_ptr = inttoptr i64 %old_raw to i64*
  store i64 %old_head, i64* %old_next_ptr
  ; Info without mark bit
  %clean_info = and i64 %info, -2
  %old_info_addr = add i64 %old_raw, 8
  %old_info_ptr = inttoptr i64 %old_info_addr to i64*
  store i64 %clean_info, i64* %old_info_ptr
  ; Normal GC magic sentinel
  %old_magic_addr = add i64 %old_raw, 16
  %old_magic_ptr = inttoptr i64 %old_magic_addr to i64*
  store i64 6557403441622859503, i64* %old_magic_ptr
  ; Update gc_head
  store i64 %old_raw, i64* @__gc_head
  ; Copy user data
  %nursery_user = add i64 %pos, 24
  %old_user = add i64 %old_raw, 24
  br label %copy_loop

copy_loop:
  %ci = phi i64 [0, %do_promote], [%ci_next, %copy_body]
  %copy_done = icmp uge i64 %ci, %size
  br i1 %copy_done, label %install_fwd, label %copy_body

copy_body:
  %src_addr = add i64 %nursery_user, %ci
  %src_ptr = inttoptr i64 %src_addr to i8*
  %byte = load i8, i8* %src_ptr
  %dst_addr = add i64 %old_user, %ci
  %dst_ptr = inttoptr i64 %dst_addr to i8*
  store i8 %byte, i8* %dst_ptr
  %ci_next = add i64 %ci, 1
  br label %copy_loop

install_fwd:
  ; Store new user ptr in nursery header[0] as forwarding pointer
  %fwd_ptr = inttoptr i64 %pos to i64*
  store i64 %old_user, i64* %fwd_ptr
  ; Forwarding sentinel in header[16]
  %fwd_magic_addr = add i64 %pos, 16
  %fwd_magic_ptr = inttoptr i64 %fwd_magic_addr to i64*
  store i64 6557438972390461613, i64* %fwd_magic_ptr
  ; Track promoted bytes in old gen
  %tb = load i64, i64* @__gc_total_bytes
  %tb_new = add i64 %tb, %old_alloc_size
  store i64 %tb_new, i64* @__gc_total_bytes
  br label %advance

advance:
  %next_pos = add i64 %pos, %obj_total
  ; Decrement alloc count and track freed bytes for dead objects
  %was_dead = icmp eq i64 %mark, 0
  br i1 %was_dead, label %dec_dead, label %walk_loop

dec_dead:
  %ac = load i64, i64* @__gc_alloc_count
  %ac_new = sub i64 %ac, 1
  store i64 %ac_new, i64* @__gc_alloc_count
  %fb = load i64, i64* @__gc_freed_bytes
  %fb_new = add i64 %fb, %obj_total
  store i64 %fb_new, i64* @__gc_freed_bytes
  %tb2 = load i64, i64* @__gc_total_bytes
  %tb2_new = sub i64 %tb2, %obj_total
  store i64 %tb2_new, i64* @__gc_total_bytes
  br label %walk_loop

done:
  ret void
}

; Check if a nursery pointer has been forwarded; return new location or 0
define private i64 @__gc_get_forwarded(i64 %user_ptr) optnone noinline {
entry:
  %in_nursery = call i64 @__gc_is_nursery_ptr(i64 %user_ptr)
  %not_nursery = icmp eq i64 %in_nursery, 0
  br i1 %not_nursery, label %not_fwd, label %check_fwd

check_fwd:
  %header = sub i64 %user_ptr, 24
  %magic_addr = add i64 %header, 16
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %is_fwd = icmp eq i64 %magic, 6557438972390461613
  br i1 %is_fwd, label %get_new, label %not_fwd

get_new:
  %new_ptr_addr = inttoptr i64 %header to i64*
  %new_loc = load i64, i64* %new_ptr_addr
  ret i64 %new_loc

not_fwd:
  ret i64 0
}

; Update all references that point to forwarded nursery objects
define private void @__gc_minor_update_refs() optnone noinline {
entry:
  ; 1. Update shadow stack roots
  %ss_inited = load i64, i64* @__gc_shadow_stack_inited
  %no_ss = icmp eq i64 %ss_inited, 0
  br i1 %no_ss, label %update_promoted, label %update_roots

update_roots:
  %ss = load i64, i64* @__gc_shadow_stack
  %count_ptr = inttoptr i64 %ss to i64*
  %count = load i64, i64* %count_ptr
  %data_addr = add i64 %ss, 16
  %data_ptr = inttoptr i64 %data_addr to i64*
  %data = load i64, i64* %data_ptr
  br label %root_loop

root_loop:
  %ri = phi i64 [0, %update_roots], [%ri_next, %root_next]
  %root_done = icmp uge i64 %ri, %count
  br i1 %root_done, label %update_promoted, label %root_body

root_body:
  %slot_offset = shl i64 %ri, 3
  %slot_addr = add i64 %data, %slot_offset
  %slot_ptr = inttoptr i64 %slot_addr to i64*
  %root_addr = load i64, i64* %slot_ptr
  %root_null = icmp eq i64 %root_addr, 0
  br i1 %root_null, label %root_next, label %root_check

root_check:
  %val_ptr = inttoptr i64 %root_addr to i64*
  %val = load i64, i64* %val_ptr
  %fwd = call i64 @__gc_get_forwarded(i64 %val)
  %has_fwd = icmp ne i64 %fwd, 0
  br i1 %has_fwd, label %root_update, label %root_next

root_update:
  store i64 %fwd, i64* %val_ptr
  br label %root_next

root_next:
  %ri_next = add i64 %ri, 1
  br label %root_loop

update_promoted:
  ; 2. Fix internal pointers in promoted objects
  %n_start = load i64, i64* @__gc_nursery_start
  %n_end = load i64, i64* @__gc_nursery_ptr
  br label %promo_loop

promo_loop:
  %pos = phi i64 [%n_start, %update_promoted], [%next_pos, %promo_advance]
  %at_end = icmp uge i64 %pos, %n_end
  br i1 %at_end, label %update_remembered, label %promo_check

promo_check:
  %p_info_addr = add i64 %pos, 8
  %p_info_ptr = inttoptr i64 %p_info_addr to i64*
  %p_info = load i64, i64* %p_info_ptr
  %p_size = call i64 @__gc_info_size(i64 %p_info)
  %p_total = add i64 %p_size, 24
  %p_magic_addr = add i64 %pos, 16
  %p_magic_ptr = inttoptr i64 %p_magic_addr to i64*
  %p_magic = load i64, i64* %p_magic_ptr
  %is_fwd = icmp eq i64 %p_magic, 6557438972390461613
  br i1 %is_fwd, label %fix_internals, label %promo_advance

fix_internals:
  %new_loc_ptr = inttoptr i64 %pos to i64*
  %new_loc = load i64, i64* %new_loc_ptr
  %num_slots = lshr i64 %p_size, 3
  br label %fix_loop

fix_loop:
  %fi = phi i64 [0, %fix_internals], [%fi_next, %fix_next]
  %fix_done = icmp uge i64 %fi, %num_slots
  br i1 %fix_done, label %promo_advance, label %fix_body

fix_body:
  %f_offset = shl i64 %fi, 3
  %f_addr = add i64 %new_loc, %f_offset
  %f_ptr = inttoptr i64 %f_addr to i64*
  %f_val = load i64, i64* %f_ptr
  %f_fwd = call i64 @__gc_get_forwarded(i64 %f_val)
  %f_has_fwd = icmp ne i64 %f_fwd, 0
  br i1 %f_has_fwd, label %fix_update, label %fix_next

fix_update:
  store i64 %f_fwd, i64* %f_ptr
  br label %fix_next

fix_next:
  %fi_next = add i64 %fi, 1
  br label %fix_loop

promo_advance:
  %next_pos = add i64 %pos, %p_total
  br label %promo_loop

update_remembered:
  ; 3. Update remembered set entries
  %rs_inited = load i64, i64* @__gc_remembered_set_inited
  %rs_not_inited = icmp eq i64 %rs_inited, 0
  br i1 %rs_not_inited, label %done, label %do_update_rs

do_update_rs:
  %rs = load i64, i64* @__gc_remembered_set
  %rs_count_ptr = inttoptr i64 %rs to i64*
  %rs_count = load i64, i64* %rs_count_ptr
  %rs_data_addr = add i64 %rs, 16
  %rs_data_ptr = inttoptr i64 %rs_data_addr to i64*
  %rs_data = load i64, i64* %rs_data_ptr
  br label %rs_loop

rs_loop:
  %rsi = phi i64 [0, %do_update_rs], [%rsi_next, %rs_next]
  %rs_done = icmp uge i64 %rsi, %rs_count
  br i1 %rs_done, label %done, label %rs_body

rs_body:
  %rs_offset = shl i64 %rsi, 3
  %rs_entry_addr = add i64 %rs_data, %rs_offset
  %rs_entry_ptr = inttoptr i64 %rs_entry_addr to i64*
  %rs_slot = load i64, i64* %rs_entry_ptr
  %rs_val_ptr = inttoptr i64 %rs_slot to i64*
  %rs_val = load i64, i64* %rs_val_ptr
  %rs_fwd = call i64 @__gc_get_forwarded(i64 %rs_val)
  %rs_has_fwd = icmp ne i64 %rs_fwd, 0
  br i1 %rs_has_fwd, label %rs_update, label %rs_next

rs_update:
  store i64 %rs_fwd, i64* %rs_val_ptr
  br label %rs_next

rs_next:
  %rsi_next = add i64 %rsi, 1
  br label %rs_loop

done:
  ret void
}

; Clear the remembered set
define private void @__gc_remembered_set_clear() {
entry:
  %inited = load i64, i64* @__gc_remembered_set_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %done, label %clear

clear:
  %rs = load i64, i64* @__gc_remembered_set
  %count_ptr = inttoptr i64 %rs to i64*
  store i64 0, i64* %count_ptr
  br label %done

done:
  ret void
}

; Set the nursery size. If nursery is already initialized, re-allocate it.
define void @__gc_set_nursery_size(i64 %bytes) {
entry:
  store i64 %bytes, i64* @__gc_nursery_size
  ; If nursery already initialized, re-initialize with new size
  %inited = load i64, i64* @__gc_nursery_inited
  %is_inited = icmp ne i64 %inited, 0
  br i1 %is_inited, label %reinit, label %done

reinit:
  ; Free old arena
  %old_start = load i64, i64* @__gc_nursery_start
  %old_ptr = inttoptr i64 %old_start to i8*
  call void @free(i8* %old_ptr)
  ; Allocate new arena
  %arena_raw = call i8* @malloc(i64 %bytes)
  %arena = ptrtoint i8* %arena_raw to i64
  %is_null = icmp eq i64 %arena, 0
  br i1 %is_null, label %disable_nursery, label %update_arena

update_arena:
  store i64 %arena, i64* @__gc_nursery_start
  store i64 %arena, i64* @__gc_nursery_ptr
  %end_addr = add i64 %arena, %bytes
  store i64 %end_addr, i64* @__gc_nursery_end
  br label %done

disable_nursery:
  ; malloc failed, disable nursery
  store i64 0, i64* @__gc_nursery_inited
  br label %done

done:
  ret void
}

; Statistics: minor collections performed
define i64 @__gc_stat_minor_collections() {
entry:
  %v = load i64, i64* @__gc_minor_collections
  ret i64 %v
}

; Statistics: nursery bytes in use
define i64 @__gc_stat_nursery_used() {
entry:
  %inited = load i64, i64* @__gc_nursery_inited
  %not_inited = icmp eq i64 %inited, 0
  br i1 %not_inited, label %zero, label %calc

calc:
  %ptr = load i64, i64* @__gc_nursery_ptr
  %start = load i64, i64* @__gc_nursery_start
  %used = sub i64 %ptr, %start
  ret i64 %used

zero:
  ret i64 0
}

; Statistics: nursery capacity
define i64 @__gc_stat_nursery_capacity() {
entry:
  %v = load i64, i64* @__gc_nursery_size
  ret i64 %v
}

