# values.spec — the single source of truth for Saffron's NaN-box value layer.
#
# WHY THIS FILE EXISTS
#
# These 19 helpers used to be hand-copied into four LLVM IR bases
# (base.ll, base_nanbox.ll, wasm_base.ll, wasm_base_32.ll). They drifted, and the
# drift shipped: BUGS #77 (`true` printed as "false" on wasm32 because one base out
# of four untagged twice), BUGS #82/#83 (a tag helper fixed in wasm_base_32.ll while
# base_nanbox.ll lagged), BUGS #39 (a GC header difference that made a native fix
# unsound on wasm32). A helper defined once cannot disagree with itself.
#
# HOW IT WORKS
#
# Each helper gives ONE body per *discipline*, not one per file:
#
#   @identity   values are raw i64 bits; tag/untag are the identity function
#   @nanbox     values are NaN-boxed; tag/untag do real bit work
#   @both       the body does not depend on the discipline
#
# tools/gen_runtime_values.py expands those into the four bases, in place, between
# the `@generated-values:begin/end` markers. Run it after editing this file:
#
#   python3 tools/gen_runtime_values.py            # rewrite the four bases
#   python3 tools/gen_runtime_values.py --check    # verify they are up to date
#
# The generated output IS checked in. The bootstrap must keep exactly one root of
# trust (build/stage2/saffronc); making base.ll a build-time artifact would add a
# second. The generator is a developer tool plus a CI gate, never a build step.
#
# @override EXISTS TO MAKE DIVERGENCE EXPENSIVE, NOT IMPOSSIBLE
#
# A target may override a body, but only with a `reason =`. The generator rejects a
# reason-less override. Every override below is one of:
#   - a real capability difference (wasm64 has no heap type tags), or
#   - DRIFT, labelled as such, preserved verbatim only so that this file's first
#     output is byte-for-byte the behaviour already shipping.
# An override marked DRIFT is a bug with a name and an address. That is the whole
# point: before this file those differences were invisible.


@target boot     discipline=identity file=base.ll
@target native   discipline=nanbox   file=base_nanbox.ll
@target wasm64   discipline=identity file=wasm_base.ll
@target wasm32   discipline=nanbox   file=wasm_base_32.ll

[section Tag/Untag Helpers]

[helper __val_tag_int]
targets = boot native wasm64 wasm32
@identity
define i64 @__val_tag_int(i64 %n) {
entry:
  ret i64 %n
}
@nanbox
define i64 @__val_tag_int(i64 %n) {
entry:
  %masked = and i64 %n, 281474976710655
  %tagged = or i64 %masked, 9221401712017801216
  ret i64 %tagged
}

[helper __val_untag_int]
targets = boot native wasm64 wasm32
@identity
define i64 @__val_untag_int(i64 %v) {
entry:
  ret i64 %v
}
@nanbox
define i64 @__val_untag_int(i64 %v) {
entry:
  ; Three input shapes reach here, and the old version handled only the first:
  ;   1) a NaN-boxed value (top 16 bits in 0x7FF8..0x7FFF) — extract the payload
  ;   2) a raw i64 emitted directly by codegen (`add i64 0, 0`) — pass through
  ;   3) a float-tagged double from arithmetic or a `Float`-annotated variable
  ;      — convert numerically
  ; Masking a double to 48 bits gives garbage: 0.0, 1.0 and 2.0 all have an
  ; all-zero low 48 bits, so every list index computed through a `Float` counter
  ; collapsed to 0 and `list[i]` silently returned element 0 forever. This is the
  ; same logic wasm_base_32.ll already had; native was the odd one out.
  %tag_bits = lshr i64 %v, 48
  %ge_min = icmp uge i64 %tag_bits, 32760          ; 0x7FF8
  %le_max = icmp ule i64 %tag_bits, 32767          ; 0x7FFF
  %is_tagged = and i1 %ge_min, %le_max
  br i1 %is_tagged, label %extract_int, label %check_raw
extract_int:
  ; NaN-boxed: sign-extend the low 48-bit payload.
  %payload = and i64 %v, 281474976710655
  %shift_left = shl i64 %payload, 16
  %sign_ext = ashr i64 %shift_left, 16
  ret i64 %sign_ext
check_raw:
  ; Raw ints occupy only the low 32 bits — either zero-extended (non-negative)
  ; or sign-extended (negative). Both must pass through untouched; bitcasting a
  ; sign-extended -1 to double yields NaN and would return 0.
  %high_bits = lshr i64 %v, 32
  %is_small_pos = icmp eq i64 %high_bits, 0
  %is_small_neg = icmp eq i64 %high_bits, 4294967295
  %is_raw = or i1 %is_small_pos, %is_small_neg
  br i1 %is_raw, label %raw_int, label %from_float
raw_int:
  ret i64 %v
from_float:
  ; A fourth shape reaches here that the list above missed: a raw *pointer*.
  ; Coroutine frame handles, GC heap pointers and closure envs are untagged
  ; i64s that overflow 32 bits (macOS heap sits around 0x6000_0000_0000), so
  ; they are neither NaN-boxed nor "small". Reinterpreting one as a double
  ; gives a denormal — exponent field zero — and `fptosi` of a denormal
  ; truncates to 0. That silently turned every coroutine handle into 0, and
  ; `__sched_coro_done(0)` answers "done" by its null guard, so the scheduler
  ; retired all three tasks of a two-task program without resuming any of them
  ; (BUGS #38, third defect).
  ;
  ; A genuine Float never lands here with a zero exponent: 0.0 and every value
  ; codegen produces has either the NaN tag or a non-zero exponent, and a true
  ; denormal (|x| < 2.2e-308) converts to 0 anyway. So exponent == 0 with a
  ; non-zero value means pointer — pass it through untouched.
  %exp_bits = lshr i64 %v, 52
  %exp_masked = and i64 %exp_bits, 2047
  %is_denormal = icmp eq i64 %exp_masked, 0
  br i1 %is_denormal, label %raw_ptr, label %check_special
raw_ptr:
  ret i64 %v
check_special:
  ; A double. Guard NaN/Inf (exponent all ones) before fptosi, which is
  ; undefined for them.
  %is_special = icmp eq i64 %exp_masked, 2047
  br i1 %is_special, label %ret_zero, label %safe_convert
safe_convert:
  %f = bitcast i64 %v to double
  %as_int = fptosi double %f to i64
  ret i64 %as_int
ret_zero:
  ret i64 0
}
@override wasm32
reason = Differs from native in ONE respect only: no sign-extended-raw-int case, because wasm32 pointers and indices are 32-bit and zero-extended, so `high_bits == 0xFFFFFFFF` cannot arise from a raw value. The BUGS #38 denormal guard IS present in both (it lands on `raw_int` here rather than a separate `raw_ptr` block; same `ret i64 %v`). Block order also differs, cosmetically. Reconciling the remaining sign-ext gap is BUGS #82/#83 territory; until then this override is what keeps the guard from being lost.
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
  ; Value has bits in upper 32 — a float from arithmetic, or a raw pointer.
  ; A pointer reinterpreted as a double is a denormal (exponent field zero) and
  ; `fptosi` of a denormal truncates to 0, which silently zeroes coroutine
  ; handles; see the same guard in base_nanbox.ll (BUGS #38). wasm32 addresses
  ; fit in 32 bits so this is unreachable there today, but the two bases must
  ; not disagree on what an untag means.
  %exp_bits = lshr i64 %v, 52
  %exp_masked = and i64 %exp_bits, 2047
  %is_denormal = icmp eq i64 %exp_masked, 0
  br i1 %is_denormal, label %raw_int, label %check_special
check_special:
  ; Guard against NaN/Inf (exponent field all 1s)
  %is_special = icmp eq i64 %exp_masked, 2047
  br i1 %is_special, label %ret_zero, label %safe_convert
safe_convert:
  %f = bitcast i64 %v to double
  %as_int = fptosi double %f to i64
  ret i64 %as_int
ret_zero:
  ret i64 0
}

[helper __val_tag_ptr]
targets = boot native wasm64 wasm32
@identity
define i64 @__val_tag_ptr(i8* %ptr) {
entry:
  %r = ptrtoint i8* %ptr to i64
  ret i64 %r
}
@nanbox
define i64 @__val_tag_ptr(i8* %ptr) {
entry:
  ; ptrtoint widens a 32-bit pointer by zero-extension on wasm32; on a 64-bit
  ; host the mask below is what keeps the tag bits clear.
  %int_ptr = ptrtoint i8* %ptr to i64
  %masked = and i64 %int_ptr, 281474976710655
  %tagged = or i64 %masked, 9221120237041090560
  ret i64 %tagged
}

[helper __val_tag_ptr_nullable]
targets = native wasm32
@nanbox
; Tag a pointer returned by a `void*` @extern, mapping a NULL to int-tagged 0
; rather than to TAG_PTR|0. A C function that returns NULL on failure (fopen,
; fgets, strstr, ...) is guarded in Saffron with `== 0`, which lowers to a raw
; icmp against __val_tag_int(0) = 9221401712017801216. Plain __val_tag_ptr(NULL)
; is 0x7FF8000000000000 (TAG_PTR|0), a value that is not equal to that, passes
; __val_is_ptr, and is then dereferenced far from the call — BUGS #84. A
; non-NULL pointer keeps its normal TAG_PTR representation, so the ~50 other
; emit_tag_ptr sites and the String-returning void* stdlib wrappers are
; untouched; only the NULL case changes.
define i64 @__val_tag_ptr_nullable(i8* %ptr) {
entry:
  %int_ptr = ptrtoint i8* %ptr to i64
  %isnull = icmp eq i64 %int_ptr, 0
  %tagged = call i64 @__val_tag_ptr(i8* %ptr)
  %r = select i1 %isnull, i64 9221401712017801216, i64 %tagged
  ret i64 %r
}

[helper __val_untag_ptr]
targets = boot native wasm64 wasm32
@identity
define i8* @__val_untag_ptr(i64 %v) {
entry:
  %ptr = inttoptr i64 %v to i8*
  ret i8* %ptr
}
@nanbox
define i8* @__val_untag_ptr(i64 %v) {
entry:
  ; Mask off the tag bits to get the raw pointer value. inttoptr is a no-op on a
  ; 64-bit host and truncates to a 32-bit pointer on wasm32.
  %ptr_int = and i64 %v, 281474976710655
  %ptr = inttoptr i64 %ptr_int to i8*
  ret i8* %ptr
}

[helper __val_tag_float]
targets = boot native wasm64 wasm32
@both
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

[helper __val_untag_float]
targets = boot native wasm64 wasm32
@identity
define double @__val_untag_float(i64 %v) {
entry:
  %f = bitcast i64 %v to double
  ret double %f
}
@nanbox
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
@override wasm64
reason = DRIFT (latent bug): tests 0x7FF8 (TAG_PTR) where the other three test 0x7FF9 (TAG_INT). Preserved verbatim so this generator's first output is a no-op; wasm64 is identity-discipline so nothing tagged reaches it today.
define double @__val_untag_float(i64 %v) {
entry:
  ; Check if value has int tag (top 16 bits == 0x7FF8)
  %tag_bits = lshr i64 %v, 48
  %is_int = icmp eq i64 %tag_bits, 32760
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

[helper __val_tag_bool]
targets = boot native wasm64 wasm32
@identity
define i64 @__val_tag_bool(i64 %b) {
entry:
  ret i64 %b
}
@nanbox
define i64 @__val_tag_bool(i64 %b) {
entry:
  %is_true = icmp ne i64 %b, 0
  %result = select i1 %is_true, i64 9221683186994511873, i64 9221683186994511872
  ret i64 %result
}

[helper __val_untag_bool]
targets = boot native wasm64 wasm32
@identity
define i64 @__val_untag_bool(i64 %v) {
entry:
  ret i64 %v
}
@nanbox
define i64 @__val_untag_bool(i64 %v) {
entry:
  %is_true = icmp eq i64 %v, 9221683186994511873
  %result = zext i1 %is_true to i64
  ret i64 %result
}

[helper __val_nil]
targets = boot native wasm64 wasm32
@identity
define i64 @__val_nil() {
entry:
  ret i64 0
}
@nanbox
define i64 @__val_nil() {
entry:
  ret i64 9221683186994511874
}

[section Type Checking]

[helper __val_is_float]
targets = boot native wasm64 wasm32
@identity
define i1 @__val_is_float(i64 %v) {
entry:
  ; A value is a float if it's NOT a NaN (or is a real NaN from float arithmetic)
  ; Check: (v & 0x7FF0000000000000) != 0x7FF0000000000000 OR mantissa == 0
  ; Simpler: check that the upper 13 bits are NOT 0x7FF8..0x7FFA (our tag range)
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
@nanbox
define i1 @__val_is_float(i64 %v) {
entry:
  ; A value is a float if it's NOT a NaN (or is a real NaN from float arithmetic)
  ; Check: (v & 0x7FF0000000000000) != 0x7FF0000000000000 OR mantissa == 0
  ; Simpler: check that the upper 13 bits are NOT 0x7FF8..0x7FFA (our tag range)
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760         ; 0x7FF8
  %is_int = icmp eq i64 %upper, 32761         ; 0x7FF9
  %is_spec = icmp eq i64 %upper, 32762        ; 0x7FFA
  %not_ptr = xor i1 %is_ptr, true
  %not_int = xor i1 %is_int, true
  %not_spec = xor i1 %is_spec, true
  %a = and i1 %not_ptr, %not_int
  %notag = and i1 %a, %not_spec
  ; Ruling out the three tags is not enough: a heap object passed through an
  ; `Any` binding arrives as a RAW (untagged) GC pointer, so `upper` is neither
  ; the PTR tag nor int/spec — and this predicate used to call it a float.
  ; `__val_is_list`/`__val_is_map` already probe the GC magic sentinel for that
  ; case; do the same here. A raw heap address has its top 16 bits clear (heap
  ; addresses < 2^48), whereas a genuine normalized double always has a non-zero
  ; exponent there — so only when `upper == 0` is it safe to dereference v-8.
  ; (+0.0 is 0x0; positive subnormals are the sole theoretical false-deref, the
  ; same negligible risk the existing list/map probes already accept.)
  br i1 %notag, label %maybe, label %no
maybe:
  %hi_clear = icmp eq i64 %upper, 0
  br i1 %hi_clear, label %probe, label %yes
probe:
  %nonzero = icmp ne i64 %v, 0
  br i1 %nonzero, label %deref, label %yes
deref:
  %magic_addr = sub i64 %v, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %has_gc = icmp eq i64 %magic, 6557403441622859503
  %is_float = xor i1 %has_gc, true
  ret i1 %is_float
yes:
  ret i1 true
no:
  ret i1 false
}
@override wasm64
reason = wasm64 spells the same three-tag rejection with or/xor instead of and/xor. Semantically identical (verified by opt -O2: both fold to one range compare).
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
@override wasm32
reason = DRIFT: lacks the raw-GC-pointer probe that native grew in 6de2898, so a bare heap pointer reaching an `Any` binding is still misclassified as a float on wasm32.
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

[helper __val_is_int]
targets = boot native wasm64 wasm32
@both
define i1 @__val_is_int(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %result = icmp eq i64 %upper, 32761         ; 0x7FF9
  ret i1 %result
}

[helper __val_is_ptr]
targets = boot native wasm64 wasm32
@both
define i1 @__val_is_ptr(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %result = icmp eq i64 %upper, 32760         ; 0x7FF8
  ret i1 %result
}

[helper __val_is_nil]
targets = boot native wasm64 wasm32
@both
define i1 @__val_is_nil(i64 %v) {
entry:
  ; nil = 0x7FFA000000000002
  %result = icmp eq i64 %v, 9221683186994511874 ; 0x7FFA000000000002
  ret i1 %result
}

[helper __val_is_true]
targets = boot native wasm32
@both
define i1 @__val_is_true(i64 %v) {
entry:
  ; true = 0x7FFA000000000001
  %result = icmp eq i64 %v, 9221683186994511873 ; 0x7FFA000000000001
  ret i1 %result
}

[helper __val_is_bool]
targets = boot native wasm64 wasm32
@both
define i1 @__val_is_bool(i64 %v) {
entry:
  %is_t = icmp eq i64 %v, 9221683186994511873  ; true  (0x7FFA000000000001)
  %is_f = icmp eq i64 %v, 9221683186994511872  ; false (0x7FFA000000000000)
  %result = or i1 %is_t, %is_f
  ret i1 %result
}

[section Heap Object Type ID]

[helper __val_type_id]
targets = boot native wasm32
@identity
define i64 @__val_type_id(i64 %v) {
entry:
  ; For a heap pointer, read the type ID from the first field of the object
  %ptr_int = and i64 %v, 281474976710655      ; mask off tag
  %ptr = inttoptr i64 %ptr_int to i64*
  %type_id = load i64, i64* %ptr
  ret i64 %type_id
}
@nanbox
define i64 @__val_type_id(i64 %v) {
entry:
  ; For a heap pointer, check if it has a GC header (magic sentinel at ptr - 8)
  %ptr_int = and i64 %v, 281474976710655      ; mask off tag
  %magic_addr = sub i64 %ptr_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %has_gc = icmp eq i64 %magic, 6557403441622859503
  br i1 %has_gc, label %read_tag, label %is_string
read_tag:
  ; GC-managed object: read type tag from header
  %type_id = call i64 @__gc_get_type_tag(i64 %ptr_int)
  ret i64 %type_id
is_string:
  ; Plain malloc'd buffer (no GC header) = string (type 1)
  ret i64 1
}

[helper __val_class_tag]
targets = native wasm64 wasm32
@nanbox
; __val_class_tag: the per-class GC type tag of a value, or 0 if it does not
; have one. Class tags start at 10, so 0 is an unambiguous "not a class
; instance" and every caller can treat it as "I don't know" rather than
; guessing a plausible answer.
;
; Accepts a class instance in *either* representation. Codegen's class
; constructors return a bare `ptrtoint` — an untagged pointer, upper 16 bits
; zero — while a value that has been through a Map, a list, or an interpolation
; carries TAG_PTR. Only accepting TAG_PTR made every `is` on a freshly
; constructed object answer false, which is indistinguishable from the bug this
; helper exists to fix. __rt_tag_ptr in runtime.sf treats upper == 0 the same
; way, for the same reason.
;
; The guards before the magic load are the ones __gc_is_heap_ptr uses, and they
; are not optional: this is called on values of static type Any, so %v may be a
; boxed Int or a small enum tag, and loading ptr-8 off one of those is a wild
; read. Reject NUL, misalignment, and anything below 4 GB (all Darwin heap
; allocations are above it), then the magic sentinel decides.
;
; Codegen calls this rather than reading the header itself, so the header layout
; stays a runtime detail — the four IR bases do not agree on it.
define i64 @__val_class_tag(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %is_tagged = icmp eq i64 %upper, 32760       ; TAG_PTR
  %is_raw = icmp eq i64 %upper, 0              ; untagged pointer from a ctor
  %ptr_like = or i1 %is_tagged, %is_raw
  br i1 %ptr_like, label %check_align, label %not_a_class
check_align:
  %ptr_int = and i64 %v, 281474976710655       ; mask off any tag
  %align_bits = and i64 %ptr_int, 7
  %aligned = icmp eq i64 %align_bits, 0
  br i1 %aligned, label %check_bounds, label %not_a_class
check_bounds:
  %too_low = icmp ult i64 %ptr_int, 4294967296 ; 4 GB
  br i1 %too_low, label %not_a_class, label %check_gc
check_gc:
  %magic_addr = sub i64 %ptr_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %has_gc = icmp eq i64 %magic, 6557403441622859503
  br i1 %has_gc, label %read_tag, label %not_a_class
read_tag:
  %tag = call i64 @__gc_get_type_tag(i64 %ptr_int)
  ret i64 %tag
not_a_class:
  ret i64 0
}
@override wasm64
reason = CAPABILITY GAP, not drift: this base's __gc_alloc is a bare malloc that *discards* %type_tag, so there is no header to read the tag back from. Returning 0 makes __class_is_a answer false rather than loading garbage. `x is SomeClass` is therefore unanswerable on wasm64 until this base grows real headers, the way wasm_base_32.ll already has.
define i64 @__val_class_tag(i64 %v) {
entry:
  ret i64 0
}
@override wasm32
reason = Same logic as native with ONE deliberate difference: the 4 GB lower bound is not applied, because wasm32 linear memory starts near zero and the bound would reject every real pointer. The floor is 16 instead (the header size), so a user pointer is always above it. Alignment plus the magic sentinel carry the check, and a stray load cannot fault outside the sandbox -- worst case is a wrong tag, not a crash.
define i64 @__val_class_tag(i64 %v) {
entry:
  %upper = lshr i64 %v, 48
  %is_tagged = icmp eq i64 %upper, 32760       ; TAG_PTR
  %is_raw = icmp eq i64 %upper, 0              ; untagged pointer from a ctor
  %ptr_like = or i1 %is_tagged, %is_raw
  br i1 %ptr_like, label %check_align, label %not_a_class
check_align:
  %ptr_int = and i64 %v, 281474976710655       ; mask off any tag
  %align_bits = and i64 %ptr_int, 7
  %aligned = icmp eq i64 %align_bits, 0
  br i1 %aligned, label %check_low, label %not_a_class
check_low:
  ; The header is 16 bytes, so a real user pointer is at least 16.
  %too_low = icmp ult i64 %ptr_int, 16
  br i1 %too_low, label %not_a_class, label %check_gc
check_gc:
  %magic_addr = sub i64 %ptr_int, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %has_gc = icmp eq i64 %magic, 6557403441622859503
  br i1 %has_gc, label %read_tag, label %not_a_class
read_tag:
  %tag = call i64 @__gc_get_type_tag(i64 %ptr_int)
  ret i64 %tag
not_a_class:
  ret i64 0
}

[helper __val_is_string]
targets = boot native wasm64 wasm32
@both
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
@override wasm64
reason = wasm64 has no heap type tags at all (its __gc_alloc is a bare malloc with no header), so it cannot answer this and returns false.
define i1 @__val_is_string(i64 %v) {
entry:
  ; In identity mode, cannot distinguish — always false
  ret i1 false
}

[helper __val_is_list]
targets = boot native wasm64 wasm32
@identity
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
@nanbox
define i1 @__val_is_list(i64 %v) {
entry:
  ; Check for nil/zero
  %is_zero = icmp eq i64 %v, 0
  br i1 %is_zero, label %no, label %check_tagged
check_tagged:
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760
  br i1 %is_ptr, label %check, label %check_raw
check:
  %tid = call i64 @__val_type_id(i64 %v)
  %result = icmp eq i64 %tid, 2               ; TYPE_LIST
  ret i1 %result
check_raw:
  ; Could be a raw (untagged) GC pointer — check magic sentinel at ptr - 8
  %is_int = icmp eq i64 %upper, 32761
  %is_spec = icmp eq i64 %upper, 32762
  %is_nan_tagged = or i1 %is_int, %is_spec
  br i1 %is_nan_tagged, label %no, label %try_gc
try_gc:
  %magic_addr = sub i64 %v, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %has_gc = icmp eq i64 %magic, 6557403441622859503
  br i1 %has_gc, label %read_gc_tag, label %no
read_gc_tag:
  %gc_tag = call i64 @__gc_get_type_tag(i64 %v)
  %gc_is_list = icmp eq i64 %gc_tag, 2
  ret i1 %gc_is_list
no:
  ret i1 false
}
@override wasm64
reason = wasm64 has no heap type tags at all (its __gc_alloc is a bare malloc with no header), so it cannot answer this and returns false.
define i1 @__val_is_list(i64 %v) {
entry:
  ret i1 false
}

[helper __val_is_map]
targets = boot native wasm64 wasm32
@identity
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
@nanbox
define i1 @__val_is_map(i64 %v) {
entry:
  ; Check for nil/zero
  %is_zero = icmp eq i64 %v, 0
  br i1 %is_zero, label %no, label %check_tagged
check_tagged:
  %upper = lshr i64 %v, 48
  %is_ptr = icmp eq i64 %upper, 32760
  br i1 %is_ptr, label %check, label %check_raw
check:
  %tid = call i64 @__val_type_id(i64 %v)
  %result = icmp eq i64 %tid, 3               ; TYPE_MAP
  ret i1 %result
check_raw:
  ; Could be a raw (untagged) GC pointer — check magic sentinel at ptr - 8
  %is_int = icmp eq i64 %upper, 32761
  %is_spec = icmp eq i64 %upper, 32762
  %is_nan_tagged = or i1 %is_int, %is_spec
  br i1 %is_nan_tagged, label %no, label %try_gc
try_gc:
  %magic_addr = sub i64 %v, 8
  %magic_ptr = inttoptr i64 %magic_addr to i64*
  %magic = load i64, i64* %magic_ptr
  %has_gc = icmp eq i64 %magic, 6557403441622859503
  br i1 %has_gc, label %read_gc_tag, label %no
read_gc_tag:
  %gc_tag = call i64 @__gc_get_type_tag(i64 %v)
  %gc_is_map = icmp eq i64 %gc_tag, 3
  ret i1 %gc_is_map
no:
  ret i1 false
}
@override wasm64
reason = wasm64 has no heap type tags at all (its __gc_alloc is a bare malloc with no header), so it cannot answer this and returns false.
define i1 @__val_is_map(i64 %v) {
entry:
  ret i1 false
}

