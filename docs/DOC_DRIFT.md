# Doc drift checklist

Tracking doc updates owed once the in-flight language/runtime work settles, so the
eventual "make the docs current" pass (esp. the mdbook under `docs/src/`) is
concrete rather than a vague sweep. Delete an item when done; delete the file when
all are clear.

Last audited: 2026-08-07.

## Open — needs an edit

- [ ] **Playground guide: theming + no-REPL iteration.** The playground now has a
      light/auto theme toggle (committed `c0f18ad`). If any doc describes the
      playground UI, note the theme control. Low priority.

## Done

- **`Number` retired from the mdbook (2026-08-07).** BUGS #49 removed the surface
  spelling, so the deprecation notices became removal notices:
  `tutorial/variables-and-types.md`, `stdlib/reflect.md`, `reference/style-guide.md`
  now say `Number` is removed (`var x: Number` errors). The other matches
  (`map/set/string/list.md` "Number of entries", `json.md`'s JSON Number type) are
  the English word / JSON terminology, not the Saffron type — correctly left alone.
  learnxiny was already converted. mdbook builds clean.

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
