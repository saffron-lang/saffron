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
