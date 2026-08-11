# Contributing to Saffron

Saffron is a self-hosted language: the compiler is written in Saffron and lives
in `src/compiler/`. It is a plain git repository — clone it, edit, run
`./bootstrap.sh`, run the tests. Start with `CLAUDE.md` at the repo root, which
is the authoritative and current description of the build, architecture, and
language.

> The `legacy/` directory holds the retired C bytecode VM. It is **dead and
> unsupported** — do not edit it, build against it, or reference it. All work
> happens in the self-hosted Saffron compiler.

## The compiler pipeline

**Lexer → Parser → Type Checker → LLVM IR Codegen → clang / wasm-ld**

| Stage | File(s) |
|-------|---------|
| Lexer | `src/compiler/lexer.sf` |
| AST | `src/compiler/ast.sf` |
| Parser | `src/compiler/parser.sf` |
| Type Checker | `src/compiler/checker.sf` |
| Codegen | `src/compiler/codegen/` (assembled into `codegen.sf`) |
| Diagnostics | `src/compiler/diag.sf` |
| Runtime | `src/runtime/` (`runtime.sf` + `.ll` bases + host C helpers) |
| Standard library | `src/lib/*.sf` |

The `src/compiler/codegen/*_body.sf` files are `sed`-assembled into `codegen.sf`
at its `@codegen-split:` markers by `bootstrap.sh`. `codegen.sf` is also live
source: everything past the class's closing brace is top-level free functions
that hold their own AST pattern-match sites. When you change an AST node's
shape, sweep `codegen.sf` as well as every `*_body.sf`.

## Adding a language feature

1. **Syntax** — new tokens in `lexer.sf`, new AST variants in `ast.sf`, parsing in `parser.sf`.
2. **Types** — type rules in `checker.sf`.
3. **Codegen** — IR emission in the relevant `codegen/*_body.sf`.
4. **Runtime** — new helpers in `src/runtime/runtime.sf` or the `.ll` bases (all four bases if the feature is target-independent).
5. **Stdlib** — importable `.sf` files in `src/lib/`.

## The bootstrap chain

The build has a chicken-and-egg constraint: the checked-in `gen2`
(`build/stage2/saffronc`) must be able to compile the current source.

```bash
./bootstrap.sh
```

- **Stage 1** — `gen2` compiles the source into `gen3` (`build/saffronc`, the "current" compiler).
- **Stage 2** — `gen3` compiles the compiler's own source into `gen4` and checks that `gen4` can compile a program. This fixed-point check is the one that matters; stage 1 only proves that `gen2` accepts the source. `SKIP_GEN4=1` skips it (roughly halves bootstrap time) — but do not skip it when deciding whether to promote.

Stage 2 also asserts that codegen's `Int` inference fallback count is 0 on the
compiler's own source, so `codegen fell back to \`Int\` N time(s)` is a
bootstrap failure. Measure any file with `saffronc --report-unresolved`.

### The critical constraint: you cannot use new syntax in compiler source

`gen2` compiles the compiler source, so the compiler source can only use syntax
`gen2` already understands. To land new syntax:

1. **Implement** it in parser/codegen using only constructs `gen2` already handles.
2. **Bootstrap** — `./bootstrap.sh` builds `gen3` with the new capability.
3. **Test that `gen3` compiles programs using the new syntax** — `build/saffronc test_feature.sf out.ll`, then `tools/saffron run test_feature.sf`.
4. **Do NOT** use the new syntax in compiler source yet — `gen2` cannot parse it.

Only after promoting `gen3` to `gen2` may the compiler source itself use the new
syntax.

### Promoting gen2

Promotion copies a working `gen3` into `build/stage2/saffronc`. All criteria
must pass:

- `./bootstrap.sh` completes (including stage 2 — do not `SKIP_GEN4`).
- `gen3` compiles test programs correctly: `tools/saffron run test/hello_bootstrap.sf`.

```bash
./bootstrap.sh                             # verify current bootstrap passes
tools/saffron run test/hello_bootstrap.sf  # verify gen3 output runs
cp build/saffronc build/stage2/saffronc    # promote
./bootstrap.sh                             # verify promoted gen2 still bootstraps
git add build/stage2/saffronc
git commit -m "Promote gen2: <new capability enabled>"
```

See `CLAUDE.md` for the full ceremony, current `gen2` limitations, and the
bootstrap file layout.

## Running the tests

`tools/run_tests.sh` runs the suites through the real compile+link pipeline.

```bash
tools/run_tests.sh           # all suites (network + known-stale tests skipped)
tools/run_tests.sh main      # only test/*.sf — smoke / feature tests
tools/run_tests.sh pass      # only test/pass/*.sf — must compile AND run
tools/run_tests.sh fail      # only test/fail/*.sf — the compiler MUST reject these
tools/run_tests.sh --network # also run network-dependent tests
tools/run_tests.sh -v        # print the captured log for every failure
```

A file in `test/fail/` that compiles cleanly is itself a failure. New features
should add coverage: a runnable case in `test/pass/` and, where relevant, a
rejection case in `test/fail/`. Run a single test directly with
`tools/saffron run test/<name>.sf`.

The `@test` stdlib provides assertions:

```saffron
import "@test" as Test
Test.assert_eq(1 + 1, 2, "basic math")
Test.assert(true, "truth")
Test.summary()
```

## Bugs

`BUGS.md` is the list of **open** bugs; `BUGS_CLOSED.md` is the resolved
archive. The count and open set are derived, not stored — run `tools/bugs.sh` to
print them (`--check` flags any FIXED-titled entry stranded in the open file).
Closing a bug **moves** its entry from `BUGS.md` to `BUGS_CLOSED.md`. When a
claim about compiler behavior matters, probe the binary with a small repro
rather than trusting prose.
