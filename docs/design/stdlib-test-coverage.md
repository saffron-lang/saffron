# Saffron Stdlib Test-Coverage Audit

Audit date: 2026-08-06. Scope: all 70 top-level modules in `src/lib/*.sf`
(subdirectory modules `src/lib/http/*` and `src/lib/llvm/*` are noted where the
test tree exercises them, but the 70-row table is the top-level set).

Evidence was gathered by grepping `test/*.sf`, `test/pass/*.sf`, and
`test/fail/*.sf` for `import` statements and by counting the assertions each
importing file makes. Every "existing test file" cell names a real file.

---

## Part 1 — The standardized module-test convention

### Location

One file per stdlib module, named after the module, in the `test/pass/` suite:

```
test/pass/<module>_test.sf
```

`test/pass/` is the conformance suite: `tools/run_tests.sh` (see below) requires
every file in it to **compile, link, AND run to a clean exit**. Placing module
tests here — rather than in `test/` (the smoke/feature suite) or `test/fail/`
(the negative suite) — makes each one a hard gate: a regression in a module's
public API turns the suite red.

### The `@test` API

Every module test begins with `import "@test" as T` and ends with `T.summary()`.
The `@` prefix resolves to `src/lib/test.sf` relative to the compiler executable
(CLAUDE.md, "Modules and Imports"). The full assertion surface exported by
`src/lib/test.sf` is:

| Function | Signature | Behavior |
|---|---|---|
| `assert(cond, msg)` | `(Bool, String)` | passes when `cond` is true |
| `assert_eq(actual, expected, msg)` | `(Any, Any, String)` | passes when `actual == expected`; on failure prints both values |
| `assert_neq(actual, expected, msg)` | `(Any, Any, String)` | passes when `actual != expected` |
| `assert_gt(actual, expected, msg)` | `(Any, Any, String)` | passes when `actual > expected` |
| `assert_lt(actual, expected, msg)` | `(Any, Any, String)` | passes when `actual < expected` |
| `assert_contains(haystack, needle, msg)` | `(String, String, String)` | passes when `haystack.contains(needle)` |
| `section(name)` | `(String)` | prints a visual grouping heading |
| `summary()` | `()` | prints the pass/fail tally; **calls `exit(1)` if any assertion failed** |
| `test(fn)` / `run_all()` | decorator + runner | alternative registration-based style (rarely used in this tree) |

The load-bearing call is `T.summary()`: on any failure it exits non-zero
(`src/lib/test.sf:141-149`, via the `_exit(1)` extern), which is exactly what the
suite runner keys off. A module test that omits `T.summary()` can silently pass
even with failed assertions — always end with it.

### What makes a file "pass" (from `tools/run_tests.sh`)

`run_positive_test()` (lines 263-377) is applied to every `test/pass/*.sf`
(discovered by the glob loop at lines 452-458). The sequence:

1. `tools/saffron build <f>` — compile + link. A non-zero exit or missing binary
   is classified as `compile-error` / `link-error` / `invalid-ir` and fails.
2. Run the binary with a `RUN_TIMEOUT` (default 10s). Timeout, segfault
   (exit 139), or any signal is a failure.
3. **Runtime-error gate** (line 309): stdout/stderr containing `Runtime Error:`
   fails as `runtime-error`.
4. **Assertion-failure gate** (lines 313-317): output matching
   `^\s*FAIL:` **or** `[0-9]+/[0-9]+ passed, [1-9][0-9]* failed` is counted and
   fails as `assertion-failure`. This is precisely the two shapes `@test` emits —
   `_record_failure()` prints `    FAIL: <msg>` (test.sf:29-33) and `summary()`
   prints `  <p>/<t> passed, <f> failed` (test.sf:146). So an `@test` failure is
   caught two ways: the non-zero exit from `summary()` AND this output-pattern
   gate.
5. `nonzero-exit` gate (line 318), except for `mini_*` tests whose exit code is
   their computed result.
6. Optional sibling files: `<name>.exit` (expected exit code, lines 332-339) and
   `<name>.expected` (expected stdout/stderr diff, lines 363-374). Module tests
   using `@test` need neither — assertions already gate correctness.

`filter_noise()` (lines 241-243) strips `[codegen]`/`[checker]` warning lines
before the failure scan, so type-inference warnings don't cause false failures.

### What a module test should assert

Assert against the module's **public API surface** — its exported `fun`s and
`class`es (not `private`/`@extern` internals). Cover the normal path and the
documented edge cases (empty inputs, negative indices, not-found / nil returns,
boundary values). The strongest existing examples of this shape are
`test/test_sorted_collections.sf` (101 assertions),
`test/pass/pantry_config`-style suites, and `test/test_semver.sf` (34).

### Existing ad-hoc pattern — recommendation

Four module tests already exist but live in `test/` (the main suite), not
`test/pass/`, and none use the `_test.sf` naming:

| File | Module | Assertions |
|---|---|---|
| `test/copy_basic.sf` | `@copy` | 13 |
| `test/signal_basic.sf` | `@signal` | 3 |
| `test/thread_basic.sf` | `@thread` | 7 |
| `test/thread_mutex.sf` | `@thread` | 11 |
| `test/thread_channel.sf` | `@thread` | 11 |

**Recommendation: migrate these to `test/pass/` under the `_test.sf` convention
(`copy_test.sf`, `signal_test.sf`, `thread_test.sf`) — but do not move them as
part of this audit.** The tradeoff:

- *In favor of migrating:* the main `test/*.sf` suite and `test/pass/*.sf` are
  run by the same `run_positive_test()` machinery, so behavior is identical
  today — but the *intent* of `test/pass/` is "conformance that must stay green,"
  and module API tests belong there. Consolidating also lets a future reader find
  every module test in one predictable place by name.
- *Against / caution:* `@thread` is native-only and `@signal` spawns real signal
  handlers; both can be timing-sensitive. Before moving them into the
  must-stay-green suite, confirm they run deterministically under the 10s
  `RUN_TIMEOUT` on CI, or they will make `test/pass/` flaky. `signal_basic.sf`
  has only 3 assertions, so migrating it should be paired with deepening it.

---

## Part 2 — Coverage table

Status legend: **COVERED** = a dedicated test file whose assertions target the
module's exported API across several functions; **PARTIAL** = exercised only
incidentally by other tests, only one or two functions, or a "dedicated" file
that is print-only / network-skipped / non-asserting; **NONE** = no test in the
tree imports or exercises it. When COVERED vs PARTIAL was ambiguous the audit
chose PARTIAL and says why.

Two structural notes that recur below:
- `list.sf`, `map.sf`, `string.sf`, `bool.sf`, `int.sf`, `float.sf` are the
  *class-prototype* modules (`docs/design/runtime-v2.md` Phase D). Nothing imports
  `@list`/`@map`/`@bool`/`@int`/`@float`; user code uses the **builtin** `List`,
  `Map`, `String`, etc., which are exercised pervasively but are compiler
  builtins, not these files. `string.sf` (the `CStr` prototype) is the one
  exception — it *is* imported and tested.
- `prelude.sf` is auto-imported into every program by the compiler
  (`src/compiler/main.sf:1760-1773`), so its interfaces are exercised implicitly
  by essentially every test, but no test imports `@prelude` directly.

| Module | Status | Existing test file(s) | What it does |
|---|---|---|---|
| args.sf | NONE | — | CLI argument parsing (flags, options, positionals) |
| ast.sf | PARTIAL | `pass/expr_spans.sf`, `pass/module_type_reexport.sf`, `fail/enum_binding_type_qualified.sf` (all `import "@ast"`) | AST node types / syntax-tree utilities; imports test re-export & span behavior, not `@ast`'s own API |
| async.sf | PARTIAL | `async.sf`, `async_coop.sf`, `test_actor_*.sf`, `test_await_loop_once.sf`, `pass/async_await_function.sf`, `pass/async_module_coro_call.sf`, `test_httpx.sf` (~12 files) | Cooperative async primitives; broadly exercised (actor tests assert), but no dedicated suite over `Async.*` surface |
| base64.sf | COVERED | `test_base64.sf` (21 asserts) | Base64 encode/decode (RFC 4648) |
| bool.sf | NONE | — | `Bool` prototype class; builtin Bool used instead, `@bool` never imported |
| bytes.sf | COVERED | `test_buffer.sf` (Buffer.alloc/from_hex/from_string/from_list/reader + get/set/slice/concat/xor/to_hex) | `Buffer` fixed-size mutable byte array |
| check.sf | PARTIAL | `pass/check_module_imports.sf` (uses `Check.check`, `Check.has_errors` only); incidental in `pass/unused_var_*.sf` | Type-check Saffron source; only 2 of its functions touched |
| color.sf | NONE | — | ANSI terminal color/style utilities |
| compile.sf | NONE | — | Compile Saffron source to LLVM IR (mentioned only in a comment) |
| concurrent_map.sf | NONE | — | Concurrent-safe Map for async tasks |
| copy.sf | COVERED | `copy_basic.sf` (13 asserts; `import "@copy"` + `{ Cloneable }`) | Shallow/deep value copying |
| crypto.sf | NONE | — | Cryptographic hashing via system utilities |
| csv.sf | NONE | — | CSV parse/serialize with quoted fields |
| datetime.sf | NONE | — | DateTime handling: format/parse/arithmetic |
| deque.sf | PARTIAL | `test_collections.sf` (only `Deque.new`/`Deque.from`), `iterators.sf` (`Deque.from`) | Double-ended queue; construction only, no push/pop asserts |
| dns.sf | PARTIAL | `test_dns.sf` (0 asserts, print-only, **network-skipped** by default) | Async DNS resolution; never actually runs in default CI |
| env.sf | NONE | — | Build environment access |
| find.sf | NONE | — | Recursive file discovery by name/ext/content |
| float.sf | NONE | — | `Float` prototype class; builtin Float used, `@float` never imported |
| fmt.sf | NONE | — | String formatting: templates, padding, alignment |
| formatter.sf | COVERED | `pass/formatter_fidelity.sf` (16 asserts) | Source-faithful Saffron code formatter |
| future.sf | PARTIAL | `imports.sf` (`import "@future"`, incidental) | Future/promise primitive for async result passing |
| gc.sf | COVERED | `gc_api_test.sf` (exercises `GC.collect/enable/disable/alloc_count/total_bytes/threshold/set_threshold`, has `.expected`), `pass/gc_constructor_rule.sf` (asserts) | GC control |
| glob.sf | COVERED | `test_glob.sf` (24 asserts) | Glob pattern matching / file discovery |
| heap.sf | PARTIAL | `test_collections.sf` (only `Heap.max`/`Heap.min`), `pass/cmp_any_ordering.sf`, `pass/fun_field_call.sf` (incidental) | Binary heap / priority queue |
| int.sf | NONE | — | `Int` prototype class; builtin Int used, `@int` never imported |
| io.sf | COVERED | `pass/io_module_wrappers.sf` (13 asserts), `pass/binary_file_bytes.sf`, `pass/io_explicit_alias.sf`, `pass/http_binary_response.sf`, `test_file.sf` | Standard I/O; well-exercised on wrappers + file I/O, though full surface not systematic |
| iter.sf | PARTIAL | `comprehensive.sf`, `goals.sf`, `pass/named_imports.sf`, `pass/import_forms_ok.sf`, `pass/import_alias_punctuation.sf` | Iterator HOFs; **26 exported fns but only `map`/`filter`/`reduce`/`sum` exercised** — large untested surface |
| json.sf | COVERED | `test_json.sf` (10 asserts) | JSON parse/serialize (`json.sf` itself is a known-segfaulting repro, not a clean test) |
| lang.sf | NONE | — | Full compiler-pipeline access (`@lang` mentioned only in a comment) |
| lexer.sf | NONE | — | Tokenize source; `@lexer` never imported (lexer is exercised indirectly by the compiler, not via this module) |
| list.sf | NONE | — | `List` prototype class; builtin List used everywhere, `@list` never imported |
| llvm.sf | PARTIAL | `test/llvm_lib/*.sf` import the **submodules** (`llvm/module`, `llvm/function`, `llvm/block`, `llvm/types`, `llvm/instructions`, `llvm/nanbox`); top-level `@llvm` never imported | LLVM IR generation library |
| log.sf | COVERED | `test_log.sf` (24 asserts) | Structured logging: levels, formatters, outputs |
| map.sf | NONE | — | `Map` prototype class; builtin Map used everywhere, `@map` never imported |
| math.sf | COVERED | `stdlib_math_sf.sf` (28 asserts), `pass/math.sf`, `pass/module_member_valid.sf`, others | Math constants/functions |
| net.sf | PARTIAL | `test_net.sf` (10 asserts, **network-skipped**), `test_async_io.sf` (network-skipped) | Unified networking (TCP/TLS/DNS); dedicated test never runs in default CI |
| os.sf | COVERED | `stdlib_os_sf.sf` (9 asserts), `pass/os_ordinary_import.sf`, `pass/cli_positional_input.sf`, many incidental | OS interaction |
| pantry_config.sf | COVERED | `pantry_config.sf` (29 asserts), `pass/stdlib_internal_helper.sf` | Build-config API for `pantry.sf` manifests |
| parser.sf | PARTIAL | `pass/expr_spans.sf`, `pass/module_type_reexport.sf` (both `import "@parser"`) | Parse source into AST; tests target span/re-export, not systematic parser API |
| path.sf | PARTIAL | `pass/import_forms_ok.sf` (`import { names } from "path"`, incidental) | Cross-platform path manipulation |
| prelude.sf | PARTIAL | none directly; **auto-imported by the compiler** (`main.sf:1760`) so exercised implicitly by every operator-overloading / duck-typing test | Operator-overloading & duck-typing protocol interfaces |
| process.sf | NONE | — | Full subprocess control (spawn/stream/manage) |
| promise.sf | PARTIAL | `pass/async_await_function.sf` (`import "@promise"`, incidental) | Promise-style combinators over async tasks |
| pubsub.sf | NONE | — | Typed publish/subscribe messaging between tasks |
| queue.sf | PARTIAL | `test_collections.sf` (only `Queue.new`/`Queue.from`) | FIFO queue; construction only |
| random.sf | COVERED | `stdlib_random_sf.sf` (8 asserts over `int/float/choice/sample/seed/shuffle`) | Random number generation |
| reflect.sf | PARTIAL | `test_reflect.sf` (0 asserts, documented as non-linking), `pass/deep_deserialize.sf` (uses `Reflect.construct/fields/field_types` but 0 asserts) | Runtime type introspection; effectively unasserted |
| regex.sf | NONE | — | Regular expressions via POSIX `regex(3)` |
| scheduler.sf | PARTIAL | `pass/gc_coro_root_order.sf`, `pass/module_extern_dispatch.sf`, `test_async*.sf`, `test_httpx.sf` (internal plumbing, incidental) | Cooperative task scheduler for native async |
| semver.sf | COVERED | `test_semver.sf` (34 asserts) | Semantic Versioning 2.0 parse/compare/match |
| set.sf | NONE | — | Set backed by Map (union/intersection/difference); `@set` never imported |
| signal.sf | PARTIAL | `signal_basic.sf` (3 asserts, ad-hoc in `test/`) | POSIX signal handling; shallow |
| sorted_map.sf | COVERED | `test_sorted_collections.sf` (101 asserts), `oracle_any_compare.sf`, `pass/sorted_map_non_ascii.sf` | Sorted map via binary search |
| sorted_set.sf | COVERED | `test_sorted_set.sf` (37 asserts), `test_sorted_collections.sf`, `pass/cmp_any_ordering.sf` | Sorted set via binary search |
| sqlite.sf | NONE | — | SQLite access via the `sqlite3` CLI |
| ssl.sf | NONE | — | TLS/SSL optional secure connections |
| stack.sf | PARTIAL | `test_collections.sf` (only `Stack.new`/`Stack.from`) | LIFO stack; construction only |
| string.sf | COVERED | `pass/checker_cstr_module.sf` (asserts over `CStr.contains/starts_with/ends_with/index_of/...`) | `CStr` prototype (NOT the builtin String); the imported/tested prototype |
| supervisor.sf | NONE | — | Monitors spawned tasks, restarts on crash |
| sync.sf | NONE | — | Async-aware synchronization primitives |
| tar.sf | NONE | — | Create/extract tar.gz via system `tar` |
| template.sf | NONE | — | Mustache-style string template rendering |
| test.sf | COVERED | dogfooded by all 179 `import "@test"` files | The test framework itself |
| thread.sf | COVERED | `thread_basic.sf` (7 asserts), `thread_mutex.sf` (11 asserts), `thread_channel.sf` (11 asserts) — ad-hoc in `test/` | OS threads (native only): spawn/join/detach/sleep, Mutex, Atomic, Channel |
| time.sf | COVERED | `stdlib_time_sf.sf` (15 asserts), `pass/iter_arrow_param_split.sf` | Time utilities |
| toml.sf | COVERED | `toml_test.sf` (27 asserts) | TOML parse/serialize |
| url.sf | COVERED | `test_url.sf` (24 asserts) | URL parse/construct/manipulate (RFC 3986) |
| uuid.sf | COVERED | `test_uuid.sf` (19 asserts) | UUID generate/parse (RFC 4122) |
| watch.sf | NONE | — | Filesystem watcher (kqueue/inotify) |

**Tally (at audit date): 23 COVERED, 19 PARTIAL, 28 NONE (of 70).** The `@thread`
row is COVERED via three ad-hoc files in `test/`.

---

## Part 3 — Prioritized backfill order

Ranking weighs (a) how load-bearing the module is — imported by many others, by
the compiler, or a core data structure; (b) how much untested public surface it
has; (c) how error-prone it looks (parsers, byte/pointer work, concurrency).
Modules already COVERED are excluded.

### Tier 1 — Core data structures & iteration used everywhere (do first)

These are the workhorses; a regression here breaks a huge fraction of programs,
and several have large exported surfaces that are almost entirely unasserted.

- **iter.sf** — 26 exported functions, only 4 (`map`/`filter`/`reduce`/`sum`)
  ever exercised. `flat_map`, `zip`, `enumerate`, `take`, `skip`, `chunk`,
  `group_by`, `frequencies`, `unique`, `sort_by`, `zip_with`, `max`, `min`,
  `join` etc. have zero coverage despite being high-traffic HOFs. Biggest
  surface-area gap in the stdlib.
- **set.sf** — NONE, yet it is a core collection with non-trivial logic (union /
  intersection / difference over a Map backing). No test imports it at all.
- **deque.sf / queue.sf / stack.sf** — currently PARTIAL but only their
  constructors are touched; push/pop/peek/amortized-O(1) behavior is untested.
  Cheap to backfill and they are common building blocks.
- **heap.sf** — PARTIAL; only `max`/`min` construction touched, no
  push/pop/comparator-ordering assertions on a priority queue.

### Tier 2 — Compiler-facing / self-hosting modules (correctness of the toolchain)

Saffron is self-hosted; these modules are the compiler's own surface and are
error-prone (parsers, type checks, IR emission). They are currently exercised
only incidentally.

- **reflect.sf** — PARTIAL but effectively unasserted, and `test_reflect.sf` is
  documented as not even linking. Reflection underpins `@json`/deserialization;
  it needs a real asserting suite.
- **parser.sf** — PARTIAL; only span/re-export behavior touched. A parser with no
  systematic round-trip/AST-shape assertions is high risk.
- **check.sf** — PARTIAL; only `check`/`has_errors` reached. The type checker's
  public entry points deserve targeted positive/negative cases.
- **ast.sf** — PARTIAL; imported for re-export tests, not for its node
  constructors/utilities.
- **lang.sf / compile.sf / lexer.sf** — NONE as importable modules. Lower than
  the above because the underlying lexer/compile paths are exercised indirectly
  by the bootstrap, but the `@`-module wrappers themselves have zero direct tests.
- **llvm.sf** — PARTIAL; submodules are tested via `test/llvm_lib`, but the
  top-level `@llvm` facade is unimported.

### Tier 3 — Widely-used services & formats with real logic

Load-bearing for real programs, non-trivial parsing/formatting, currently
untested.

- **regex.sf** — NONE; POSIX regex wrapper, easy to get edge cases wrong
  (anchors, groups, no-match).
- **csv.sf** — NONE; quoted-field parsing is a classic bug nest.
- **fmt.sf** — NONE; padding/alignment/template formatting, used for output.
- **datetime.sf** — NONE; date arithmetic and parse/format are error-prone.
- **crypto.sf** — NONE; hashing correctness matters and is testable against known
  vectors.
- **args.sf** — NONE; CLI parsing (flags/options/positionals) has many edge
  cases and is user-facing.
- **process.sf** — NONE; subprocess spawn/stream/exit-code handling.
- **path.sf** — PARTIAL; only incidental `names()` use, no join/split/normalize
  assertions.

### Tier 4 — Concurrency & async surface (broaden beyond incidental)

Heavily used but only *incidentally* exercised; deserve dedicated assert suites
rather than relying on actor/await side-effects. Native/timing sensitivity makes
these trickier to make deterministic (see Part 1 migration caution).

- **async.sf** — PARTIAL but pervasive; add a dedicated `Async.*` API suite.
- **scheduler.sf** — PARTIAL; internal but central to native async.
- **sync.sf**, **concurrent_map.sf**, **pubsub.sf**, **supervisor.sf**,
  **future.sf**, **promise.sf** — the async support cast: NONE (or incidental for
  future/promise). Prioritize `sync` and `concurrent_map` (shared mutable state,
  race-prone) over `supervisor`/`pubsub`.
- **signal.sf** — PARTIAL (3 asserts); deepen alongside migration to `test/pass/`.

### Tier 5 — Network / IO-adjacent (test-infra dependent)

Real logic but their existing tests are network-skipped or the modules require
external state, so they need fixtures or a `--network` lane rather than plain
`test/pass/` files.

- **net.sf** — PARTIAL but network-skipped; the pure-logic parts (address/parse
  helpers) could be split out and asserted offline.
- **dns.sf** — PARTIAL, print-only + network-skipped.
- **ssl.sf** — NONE; depends on TLS availability.
- **tar.sf**, **sqlite.sf** — NONE; shell out to `tar`/`sqlite3`, need fixtures.
- **watch.sf** — NONE; filesystem-event driven, hard to make deterministic.
- **find.sf** — NONE; testable with a fixture directory tree (glob.sf, its
  neighbor, is already COVERED).

### Tier 6 — Prototype/builtin-shadow & cosmetic (lowest priority)

- **list.sf / map.sf / bool.sf / int.sf / float.sf** — NONE, but these are the
  runtime-v2 *prototype* classes; the builtins they mirror are exercised
  everywhere. Only worth testing when Phase D actually swaps them in. Until then,
  low value.
- **string.sf (CStr)** is already COVERED and is the live prototype.
- **color.sf** — NONE; cosmetic ANSI output, low risk.
- **env.sf** — NONE; thin environment accessor.
- **template.sf** — NONE; has real parsing logic (Mustache) so arguably belongs in
  Tier 3, but lower traffic — bump up if it gains users.
- **prelude.sf** — PARTIAL but auto-imported and implicitly exercised by every
  operator-overloading test; a dedicated suite adds little.
