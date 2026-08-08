# Differential oracle sweep — 2026-08-08 (HEAD 64e7354)

Full `tools/differential.sh` (native-O2 vs native-O0 vs wasm32) over `main` + `pass`.
**464 agree, 19 mismatch, 2 nondet, 60 capability-skip, 10 ref-fail.**

## Category A — native-O2 vs native-O0 (MOST severe: pure-native, opt-dependent)

Both deterministic across 3 runs. Symptom shared: `dispatch 'to_string' on untyped
value` warnings; the closure/GC-root family (#80/#115). O2 folds away a read of
uninitialized / mistagged memory that O0 keeps.

1. **test_log** — O0 **segfaults (139)**, O2 exits 0. Minimal repro `/tmp/repro_testlog.sf`
   (15 lines): a `@log` MemoryHandler used, cleared, then a Logger with a fresh
   MemoryHandler added and `logger.info("hello")` called. The prior MemoryHandler
   allocations are load-bearing — without them it survives. Crash is at `logger.info`
   → `_log` → `this.handlers[i].handle(record)` (interface dispatch over `List<Handler>`).
2. **pass/segv_name_collision_fun_vs_var** — O0 exits 1 (assertion FAIL: capturing
   nested fun's captured local reads `5.21502e-310`, a raw closure-pair pointer as a
   denormal), O2 passes 13/13. The `#80` capturing-nested-fun trampoline loses its
   captured local at O0. This is a regression-test file for #80 that now half-regresses
   at O0 only.

## Category B — real wasm32 traps (`unreachable`), no host dependency

Import only `@test`/`@iter`/`@math` (all wasm-supported), pass on native, trap on
wasm32. NOT capability-gate cases. All show a full-file dependency — minimal
one-liners of the "same" construct pass, so heap-layout / GC-root sensitive.

3. **pass/int_to_float_widening** — native 24/24, wasm32 `unreachable`. Int→Float
   widening (#54/#28) somewhere in the full 134-line file; `f():Float{return 0}` and
   `(0.0 + int)` in isolation both pass on wasm32.
4. **stdlib_io** — wasm32 `unreachable` (but imports only `@test`; verify it isn't
   secretly touching io builtins).
5. **pass/dispatch_readln_is_a_string** — wasm32 `unreachable` (name suggests readln;
   confirm whether it's a genuine host dep that should be gated, vs a real trap).
6. **pass/unresolved_index** — wasm32 `unreachable`.
7. **pass/import_alias_punctuation** — wasm32 `unreachable` (imports `@iter`/`@math`).

## Category C — real wasm32 output-content divergence (wrong answer, not trap)

8. **trailing_closure_params** — native prints `10`, wasm32 prints `0`. HIGH interest:
   a silent wrong numeric answer.
9. **pass/formatter_fidelity** — native "All 15 assertions passed", wasm32 "All 13".
   Two assertions silently pass-through / are skipped on wasm32.
10. **nullable_narrowing** — wasm32 prints an extra line vs native. A truthy check
    `if (result3)` on a `String|Nil` holding `"yes"` fires on wasm32 but not native,
    but ONLY in the full-file ordering (isolated, both skip it). Fragile
    slot-reuse-dependent divergence.

## Category D — capability-gate misses (harness, likely not bugs)

`@thread` (thread_basic/channel/mutex) and `@toml` (toml_test) genuinely need host
features the wasm shim lacks → should be `skip-wasm`, not MISMATCH. The gate list in
`differential.sh` (`WASM_UNSUPPORTED_IMPORTS`) omits `@thread` and `@toml`. Fix: add
them to the gate. Low risk, no compiler change.

## Category E — known / by-design

- **mini_1param/arithmetic/ifelse/while** — exit-code assertions; the wasm JS runner
  (`tools/oracle/wasm_run.mjs`) calls `_start()` and discards main's return, and
  `_start` in `wasm_base_32.ll` is `ret void` (drops `__saffron_boot`'s i64). So exit
  codes are structurally invisible on wasm32. Either give these `.expected`/stdout
  assertions instead of exit-code, or teach the runner+`_start` to propagate an exit
  code. Harness/design, not a codegen bug.
- **async** — bare-Future-print garbage (`5.21502e-310`), known #115/#154 pointer-to-
  formatter family.

## Category F — ref-fails (don't build with reference compiler)

`builtin_types, for_in, goals, loops, runner, types, decorators, any_bug_repro,
test_package_import` — several are known-stale (aspirational syntax, in STALE_TESTS).
`test_net` exits 1 (network). Confirm each is intentionally-stale vs newly-broken.

## Nondet (excluded, correctly)
`functions`, `gc_roots_test` — reference disagrees with itself run-to-run (frame layout).

## ROOT CAUSE — segv_name_collision (Category A #2) — DIAGNOSED

Minimal repro (`/tmp/capnest.sf`, 9 lines): a named nested fun returned by name that
captures an enclosing local returns garbage at -O0, correct at -O2:
```
fun make_b() {
    var inner: String = "captured"
    fun collide_b() { return inner }
    return collide_b
}
var collide_b = make_b()
IO.println(collide_b())   // O0: 5.21502e-310 (dangling), O2: "captured"
```

Mechanism (verified in emitted IR):
- `gen_func_ref` (src/compiler/codegen/expr_body.sf:1540-1565) builds a nested fun's
  closure env by storing `typed_ptr_to_val("%local","i64*")` — the ADDRESS of the
  enclosing STACK slot (line 1560), not the value. Env is `__sf_malloc`'d (untracked).
- The trampoline (1601-1602) loads that address and forwards it; the real
  `make_b$$collide_b(i64 %inner.arg)` does `inttoptr %inner.arg` + `load`. So the
  nested-fun capture convention is BY-REFERENCE (pass the slot pointer).
- In-place calls are fine (frame alive). But a RETURNED nested fun escapes: `make_b`
  returns and its stack frame (holding `%inner`) dies, leaving the env pointing at a
  dead slot. -O2 keeps the value around by luck; -O0 clobbers it → denormal garbage.

Contrast the LAMBDA path (closures_body.sf:116-143) which correctly COPIES the value
for non-boxed captures and only stores a cell POINTER for `boxed_locals` (heap cells
that survive the frame, BUGS #51). gen_func_ref never consults boxed_locals and
always captures by-ref.

Proposed fix options:
  (a) When a named nested fun's ref ESCAPES (returned / stored, not called in-place),
      box its captured enclosing locals in the enclosing frame — same treatment the
      lambda path gives `boxed_caps` — so the env holds a surviving heap-cell pointer.
      Requires the enclosing fn to mark those locals boxed (the boxed_locals decision).
  (b) Make gen_func_ref copy by VALUE like the lambda path — but the real function
      signature expects a pointer (inttoptr+load), so the callee lowering would also
      need to change for the escaping case. Option (a) is more consistent with #51.

This is the SAME by-reference-capture family the test_log O0 segfault likely sits in
(default-param temp / handler dispatch also allocate + capture). Fixing gen_func_ref's
escape case may resolve both. Compiler change → needs rebootstrap + gen2 promotion.
Related: long-standing BUGS #2 (forward refs in nested closures) and #51 (boxed cells).
