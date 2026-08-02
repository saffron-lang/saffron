# The differential test oracle

## The problem it exists to solve

Every remaining bug class in this compiler is a **silent wrong answer**. Not a
crash, not a diagnostic — a program that compiles cleanly, exits 0, and prints
something untrue. Two examples from the week this was written:

- **BUGS #102** — an enum's auto-generated `to_string()` decoded a one-field
  enum's heap pointer as a `tag << 56` immediate, so *every* value of such an
  enum stringified as its first variant with the low 56 bits of the pointer as
  the "payload". It compiled fine and had presumably been wrong for months.
- **BUGS #77** — on wasm32 only, `__bool_to_string` untagged an already-untagged
  value, so every `true` printed as `false` while *branching* on the same value
  stayed correct. Half the cases looked right by accident, because `false`
  rendered correctly.

Neither is findable by asking "does it compile". Neither is findable by a
bootstrap, because the compiler self-hosts in `--identity-mode`, where the
tag/untag emitters are no-ops (`src/compiler/codegen/types_body.sf:606-618`) —
the entire value-representation layer that both bugs live in is bypassed when the
compiler compiles itself.

Finding this class needs an **oracle**: something that says what a program
*should* print, independently of the codegen path under test.

## The cheap oracle: two backends, one stdout

The full oracle is a reference interpreter, and that is a large piece of work.
But most of the value comes from a much cheaper observation:

> The same `.sf` file, compiled and run two different ways, must produce
> byte-identical stdout.

No reference implementation is needed. Neither configuration is privileged, so a
disagreement does not say *which* side is wrong — but it says, with no judgement
call and no expected-output file to maintain, that **one of them is**. That alone
would have caught #77 immediately and for free.

`tools/differential.sh` implements this. Configurations:

| Config | What it is | What it isolates |
|---|---|---|
| `native-O2` | what users actually run | the reference |
| `native-O0` | same runtime, no optimizer | optimizer-visible UB, and answers that depend on heap layout |
| `wasm32` | different runtime base, different host | target-specific tag/untag drift (mechanism M5) |

`native-O0` earns its place: it is the axis that caught the `Any`-comparison bug
below, and being pure-native it cannot be explained away as a wasm or FFI
problem.

### Running it

```bash
tools/differential.sh                        # every suite, every configuration
tools/differential.sh test/lists.sf          # one file
tools/differential.sh --suite pass           # only test/pass/*.sf
tools/differential.sh --config native-O0     # only one comparison axis
tools/differential.sh -v                     # show full diffs
tools/differential.sh --record               # write .expected from the reference
```

Exit status is nonzero **only** for mismatches. Pre-existing build failures and
nondeterministic tests do not make it red, deliberately: a tool that is
permanently red gets ignored, and then it is not a tool.

## Why an oracle has to fail closed

An oracle is only worth having if a mismatch it reports is *believed*. Every way
the harness itself could manufacture a failure has to be closed off, or the tool
trains people to dismiss its output. Three gates:

**Nondeterminism gate.** The reference configuration runs twice. If it disagrees
with itself, the test is `nondet` and excluded from comparison entirely. This
catches `gc_deep_test` by *behaviour* rather than by hard-coding its name, so a
newly-flaky test is handled without editing the harness.

**Capability gate.** A test that imports `@net`, `@async`, `time`, `random`, and
so on cannot run under wasm32 at all, and "it printed nothing" is not a
disagreement about semantics. These are skipped by import string, and reported as
skips so the number stays visible instead of quietly shrinking the corpus.

**Build gate.** `build-fail-<config>` is its own category and is never merged
into the mismatch count. A test that does not compile is a different problem from
a test that compiles and lies.

This discipline was learned the hard way. The first run produced 13 mismatches,
of which **8 were the harness's own fault**: `wasm_run.mjs` stubbed unresolved
imports as `() => 0`, and returning a JS number where the module declared an i64
result throws `Cannot convert 0 to a BigInt`. Five more were stubbed libm
functions making `stdlib_math` report `round: expected 4, actual 0`. Both are
fixed — the host now reads the module's Type and Import sections to generate
signature-correct stubs, and installs real libm for imports declared all-f64. A
harness that manufactures its own failures is worse than no harness, because
every real finding then has to be argued for.

### What is deliberately NOT normalized

`normalize()` strips only `^\[(codegen|checker)\] Warning` lines. It does **not**
normalize float formatting, list or map rendering, or pointer-ish looking values.
That is exactly where the wrong answers live. Normalizing output until the
configurations agree is the failure mode this whole tool exists to avoid.

## The blind-test finding

Of **174 positive tests** in the suite:

| | count |
|---|---|
| assertions **and** `.expected` | 0 |
| assertions only | 77 |
| `.expected` only | 33 |
| **neither — would pass on ANY output** | **64** |

Those 64 are graded solely on "exit 0 and no `Runtime Error:` on stderr". They
print, and nothing checks what. All 11 `mini_*` tests are in this group, as are
`test/pass/enums.sf`, `test/pass/generics.sf`, `test/pass/closures.sf`,
`test/pass/interfaces.sf`, `test/pass/operators.sf`, `test/json.sf`, and
`test/inheritance.sf`.

This is not hypothetical. `test/pass/enums.sf` and `test/pass/generics.sf` print
lines like `7.29112e-304` and `5.21502e-310` **today**, and both are counted as
passing, because they are blind. `run_tests.sh` already carries a comment saying
`test_async.sf` "was green for the entire life of BUGS #38 while emitting 2 of
its ~12 expected lines and garbage for the rest" — the mechanism was known; the
scale was not.

`--record` closes this gap by writing `.expected` from the reference run. It
**refuses** for any test whose configurations disagree: freezing a disputed
output converts an open question into a confident wrong answer, which is worse
than having no expectation at all.

## What it found on its first real runs

Three silent bugs, none of which any existing test could see.

**1. `IO.println(enum_value)` prints a reinterpreted bit pattern.**
`IO.println(Color.Red)` prints `0`; `Color.Green` prints `7.29112e-304`; but
`IO.println("${Color.Green}")` correctly prints `Green`. The direct path emits
`shl i64 0, 56` and passes the result to `__io_println` with no `to_string()`
call and no int tag, so the runtime finds no NaN-box tag, concludes "unboxed
double", and formats the raw bits — `1 << 56` is a subnormal. Interpolation works
because the lexer's `"${e}"` desugaring inserts an explicit `.to_string()`. Lists
are broken the same way. Related to but distinct from #102: #102 was *inside*
`to_string()`, and this path never calls it. Regression test:
`test/oracle_enum_println.sf`.

**2. A match-arm binding collides with a same-named enclosing variable.** At
module scope the global lives at `@__g_s` but the arm emits
`store i64 %tN, i64* %s` against a never-allocated local — invalid IR, caught
loudly by `opt -verify`. *Inside a function* `%s = alloca i64` does exist, so the
store lands in the outer variable's slot: a `String`-declared variable silently
holds a tagged Int, `s.length()` prints `0`, exit 0, no diagnostic. The arm
binding is registered in the enclosing scope rather than an arm scope. This
obstructed building the oracle itself. Regression test:
`test/oracle_match_shadow.sf`.

**3. Relational operators on `Any`-typed operands compare untagged garbage.**
The most serious, and the only mismatch on the pure-native axis.
`test_sorted_collections.sf` reports `All 76 assertions passed` at -O2 and
`74/76 passed, 2 failed` (exit 1) at -O0, from identical source through an
identical compiler. `expr_body.sf:1127` gates the `__safe_strcmp` path on a
*static* type test; two operands declared `Any` — what every generic container
has — fall through to `__val_untag_int` + `icmp slt` and compare heap-pointer bit
patterns. -O2 passes by **heap-layout luck**: the interned literals happen to sit
in an order agreeing with alphabetical order.

Scope is wider than the `sorted_*` modules. `rt_list_sort`
(`runtime.sf:1759`) compares with `if (a > b)` on `Int`-declared locals holding
NaN-boxed values, so **`List.sort()` does not sort strings at all**, at either
optimization level, with no error. The suite's only two `.sort()` calls
(`lists.sf:19`, `test_stdlib.sf:39`) sort numbers, which is why this survived.
`T.assert_lt`/`assert_gt` in the test library itself take `Any` and are therefore
unreliable on strings — latent, since no test currently calls them that way.

`sorted_map.sf` vs `sorted_set.sf` is a controlled experiment for the mechanism:
same binary search over string keys, but `sorted_map` declares `key: String` and
routes through a hand-rolled `_str_cmp` over an `_alphabet` table whose comment
says it exists "enabling correct `<` ordering" — someone hit this and worked
around it *in the library* rather than fixing the compiler. `sorted_map` is
right; `sorted_set`, which uses bare `<` on `Any`, is wrong. Regression test:
`test/oracle_any_compare.sf`, which fails 16/41 at -O0 and 1/41 at -O2 with every
control passing. The single case still failing at -O2 is a reverse-alphabetical
insert added specifically so that layout luck cannot rescue it.

## Status and honest limits

**Built and working:** the multi-configuration harness, the wasm32 host with
signature-correct imports and real libm, the three fail-closed gates, `--record`,
and four oracle test files.

**Not built: the reference interpreter.** An AST-walking interpreter would be a
true oracle — able to say which side is wrong, not merely that the two disagree,
and able to grade a single configuration with no second backend. It is not
started. Everything above is the cheap approximation, and its limits follow
directly from that:

- It cannot catch a bug that is **identical in all configurations**. A wrong
  answer that is consistently wrong native and wasm, -O0 and -O2, is invisible to
  it. Bug 1 and bug 3's `List.sort()` face were both found only because a
  hand-written test asserted the *correct* answer — not by the diff.
- Tests behind the capability gate get **no cross-target comparison** at all.
- `.expected` files recorded by `--record` freeze *current* behaviour, which
  makes them regression detectors, not correctness statements.

The 64 blind tests are the largest remaining gap, and `--record` plus the
comparison above is a partial answer, not a complete one.
