# Doc drift checklist

Tracking doc updates owed once the in-flight language/runtime work settles, so the
eventual "make the docs current" pass (esp. the mdbook under `docs/src/`) is
concrete rather than a vague sweep. Delete an item when done; delete the file when
all are clear.

Last audited: 2026-08-07.

## Open — needs an edit

- [ ] **Retire `Number` from the mdbook + learnxiny.** We are dropping `Number`
      in favour of explicit `Int`/`Float` (BUGS #49). The learnxiny doc is already
      converted (`docs/learnxinyminutes/learnsaffron.sf`), but the mdbook still
      teaches `Number` in **8 files** (9 occurrences):
      `tutorial/variables-and-types.md` (×2), `stdlib/{map,json,reflect,set,list,string}.md`,
      `reference/style-guide.md`. Convert each to `Int` or `Float` per context
      (indices/counts → `Int`; real numerics → `Float`). **Blocked**: do this only
      once `Number` is actually removed from the surface syntax, not while the
      checker still maps it — otherwise the docs would describe a spelling that
      still works.

- [ ] **Playground guide: theming + no-REPL iteration.** The playground now has a
      light/auto theme toggle (committed `c0f18ad`). If any doc describes the
      playground UI, note the theme control. Low priority.

## Verified current — no action (recorded so the next audit doesn't re-check)

- `getting-started/repl.md` — correctly states there is no REPL. ✓
- `tutorial/error-handling.md` — correctly says runtime faults (IndexError,
  division, null) are **fatal / not catchable**, only `throw` is catchable
  (matches BUGS #65). ✓
- wasm targets: no mdbook page currently claims wasm-specific behaviour that the
  wasm32 fixes this session (#170 for-in, #172 malloc, #173 exceptions, #174 `>=`,
  #75 boundary tagging, #179 scheduler) would contradict. If a wasm/targets page
  is added later, note that wasm32 now supports for-in, try/catch, and larger
  heaps; wasm64 remains string-output-only (BUGS #131).

## Notes

- `CLAUDE.md` "Known Issues" is derived from `BUGS.md`/`tools/bugs.sh`, not
  hand-maintained prose — no separate doc edit needed there.
- The mdbook builds from `docs/src/` via `docs/book.toml`; rebuild with `mdbook
  build docs` after edits.
