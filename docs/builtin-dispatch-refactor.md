# Builtin Method Dispatch: Design Deep Dive

**Status:** design investigation, no production code changed.
**Date:** 2026-07-30
**Verdict up front:** **Do not do the big refactor as framed.** Do four cheap, independently shippable things instead. Evidence below, including two new live bugs found and fixed in a prototype.

All measurements were taken in an isolated tree (`/tmp/sfrefac`, from `git archive HEAD`). The real repo was read-only throughout except for this file.

---

## 0. Executive summary

The hypothesis was: *stop special-casing Map/List/String, declare them as ordinary types with real signatures, collapse ~40 branches into one dispatch path, make the terminal a hard error, eliminate the bug class at the root.*

Three measurements changed the recommendation:

1. **The terminal silent-zero fall-through fires zero times today.** Instrumented gen3 compiling the compiler itself: 0 hits. Compiling all 142 test files: 0 hits. The four shipped fixes (0b7542a, fa96fae, 06fe5c5, dbe8d81) have closed every currently-reachable path. The bug class as framed — silent `return "0"` — is **latent, not active**. Hardening the terminal into a hard error is therefore nearly free, and it removes the mechanism permanently. It does not require a refactor.

2. **The real remaining damage is not the branch count, it is a handful of specific dishonest fallbacks — and they are live.** I instrumented the suspicious sites and found two previously-unknown bugs, both reachable from ordinary user code, both reproducible on the current committed compiler:
   - `Map<K,V>.values()` / `.keys()` unconditionally claim `List<String>`. `for (v in m.values())` on a `Map<String,Int>` **segfaults**.
   - `get_expr_type` claims `slice` returns `String` on an unknown receiver. `var s = any_list.slice(0,2); s.length()` returns **1 instead of 2** (strlen on a list pointer).

   Both were fixed with **29 changed lines total** across two files, verified by bootstrap and full suite with zero regressions. That is a ~1-hour fix for two real bugs. The refactor is a multi-week project that would fix the same two bugs.

3. **`checker.sf` is a genuinely separate pass.** Codegen's `last_type` is a *dispatch hint*; soundness lives in `checker.sf`. Widening codegen fallbacks to `Any` therefore **cannot** weaken the type system. The human's concern ("excessive uses of Any may compromise the type system") is real in general but does not apply to this change — with one important caveat documented in §6.

**Recommended plan:** delete ~123 lines of dead code; harden the terminal; fix the ~6 live dishonest fallbacks; add a regression test. Skip the unification. Total ~1-2 days, four independently revertable commits.

---

## 1. Inventory

### 1.1 Which file is live

`src/compiler/codegen/methods.sf` is **NOT live**. `bootstrap.sh:115`, `:144`, `:185` each run:

```
sed -i '' '/^import "\.\/codegen\/methods\.sf"/d'
```

stripping the import, while `bootstrap.sh:113` sed-injects `codegen/methods_body.sf` into `codegen.sf` at the `// @codegen-split: methods` marker (`codegen.sf:464`). **Every live dispatch site is in `src/compiler/codegen/methods_body.sf` (3094 lines).** The `methods.sf` extend-fun copy is a parallel, unused draft. Editing it has no effect.

### 1.2 Dead code: the `builtin_methods` table was designed and never wired up

```
src/compiler/codegen.sf:59    var builtin_methods: Map<String, String>
src/compiler/codegen.sf:125   this.builtin_methods = {}
```

`grep -rn "builtin_methods" src/compiler/` finds **no other write**. Every read is therefore dead:

| Site | Lines | Status |
|---|---|---|
| `set_result_type` | methods_body.sf:552-559 | dead |
| `tag_builtin_result` | methods_body.sf:561-574 | dead |
| `emit_builtin_dispatch` | methods_body.sf:576-674 | dead |
| `builtin_methods.has(method)` guard | methods_body.sf:1923-1935 | dead branch |
| `builtin_methods.has(method)` guard + unreachable warning | methods_body.sf:1936-1960 | dead branch |

**~123 lines of free deletion.** Note what this means: a table-driven dispatcher with a pipe-encoded schema (`"pattern|func|ret|push"`, patterns `v2`/`v3`/`r1`/`r2`/`p1`/`p2`/`p3`/`m2`) was already written and then bypassed by the hand-written type-aware branches at :1643 (List), :1756 (Map), :1787 (String) — whose own comments say "replaces builtin_methods for List/Map/String". **Someone already tried the refactor and abandoned it in favour of per-type branches.** That is a strong prior against retrying it.

### 1.3 Builtin dispatch branches (live), `gen_method_call` = methods_body.sf:854-2894

`obj_type` is computed once at :2188-2192 (`type_to_string(last_type)`, overridden by `get_var_type_str(obj_name)` when non-empty). Branches before that line compute their own receiver type inline — which is the actual duplication problem.

| Method | Lines | Receiver types handled | Extern emitted | Result type set | Unknown-receiver fallback |
|---|---|---|---|---|---|
| push | 1650 | `List*`/`Any` | `__list_push` | NilType | n/a |
| pop | 1670 | `List*`/`Any` | `__list_pop` | elem type | **IntType** :1687 ⚠️ |
| remove | 1691 | `List*`/`Any` | `__list_remove` | elem type | **IntType** :1705 ⚠️ |
| reverse | 1708 | `List*`/`Any` | `rt_list_reverse` | receiver | receiver |
| copy | 1722 | `List*`/`Any` | `__list_copy` | receiver | receiver |
| sort | 1735 | `List*`/`Any` | `__list_sort` | receiver | receiver |
| join | 1745 | `List*`/`Any` | `__list_join` | StringType | StringType (ok) |
| keys | 1764 | `Map*`/`Any` | `__map_keys` | **`List<String>` always** ⚠️ **LIVE BUG** | same |
| values | 1774 | `Map*`/`Any` | `__map_values` | **`List<String>` always** ⚠️ **LIVE BUG** | same |
| trim/to_upper/to_lower | 1800-1850 | `String`/`Any` | `__str_*` | StringType | StringType (ok) |
| index_of | 1852 | `String`/`Any` | `__str_index_of` | IntType | IntType (ok) |
| repeat/split/ends_with/replace | 1860-1915 | `String`/`Any` | `__str_*` | String / `List<String>` | ok |
| append | 1961 | StringBuilder | `__sb_append` | NilType | n/a |
| set | 1970 | List vs Map by type string | `__list_set`/`__map_set` | NilType | n/a |
| has | 2009 | any | `__map_has` **always** | BoolType | ⚠️ no String/List guard |
| get | 2033-2076 | `Map<K,V>` narrows via `get_map_value_type` | `__map_get`/`__list_get` | V | **AnyType** :2073 ✅ (dbe8d81) |
| starts_with | 2077 | String only (inline strlen/strncmp) | — | BoolType | ⚠️ no guard |
| char_at | 2093 | String | — | StringType | ⚠️ no guard |
| is_upper/is_lower | 2111/2126 | String | — | BoolType | ⚠️ no guard |
| slice | 2141 | `List*`/`Any` → `__list_slice`, else string memcpy | both | receiver / String | see §1.4 ⚠️ |
| length | 2194 | `Any` → `__any_length`; `String` → `strlen` | | IntType | **`else` → `__list_length`** :2216 ⚠️ **the #32 crash mechanism** |
| contains | 2224 | List/Map loop, String → `strstr` | | BoolType | `__list_contains` |
| to_number | 2303 | any | `atol` **always** | IntType | ⚠️ no guard |
| to_float | 2314 | any | `strtod` **always** | FloatType | ⚠️ no guard |
| to_string | 2323-2399 | SB/String/Float/Bool/enum/List/Map/Any | various | StringType | **else: untag as Int** :2390 ⚠️ |
| floor/ceil/abs/round | 2400/2418/2432/2456 | Float/Int | | IntType | returns `obj`, IntType |
| super | 2472 | — | | | **StringType** :2494 ⚠️ bizarre |

### 1.4 Every remaining dishonest fallback (the highest-value output)

Ranked by measured reachability. ⚠️**LIVE** = I reproduced misbehaviour on the committed compiler.

| # | Site | Claim | Truth | Status |
|---|---|---|---|---|
| **D1** | methods_body.sf:1771, :1782 — Map `keys`/`values` | `List<String>` | `List<K>` / `List<V>` | ⚠️**LIVE BUG, segfault.** See §1.5 |
| **D2** | methods_body.sf:250-253 — `get_expr_type` `slice` | `String` when receiver not `List*` | could be List | ⚠️**LIVE BUG, wrong answer.** See §1.5 |
| **D3** | methods_body.sf:2216-2222 — `length` `else` | `__list_length` on anything not Any/String | could be Map/instance | Reachable; the #32 mechanism. `Any` is caught above so currently benign, but one bad upstream inference re-arms it |
| **D4** | methods_body.sf:249 — `get_expr_type` `length`/`index_of`/`to_number` | `Int` regardless of receiver | Int is right for all three | Benign — dishonest in form, correct in result. Leave |
| **D5** | methods_body.sf:254-256 — `to_string`/`join`/`trim`/…→String, `split`/`keys`→`List<String>`, `has`/`contains`/…→Bool | receiver-blind | `keys` is wrong (see D1); rest correct | Fix `keys` only |
| **D6** | methods_body.sf:276 — `get_expr_type` `pop` | `Int` when receiver not `List<...>` | unknown | Should be `Any` |
| **D7** | methods_body.sf:1687, :1705 — List `pop`/`remove` when elem is Any | `IntType` | `Any` | Should be `AnyType`; instrumented, did not fire in suite |
| **D8** | methods_body.sf:281 — `reverse`/`copy`/`sort` non-List | `List<Any>` | unknown | Mild; `List<Any>` routes to runtime dispatch anyway |
| **D9** | methods_body.sf:154 — `map_lit` | `Map<String,String>` | element types from literal | Produces "dispatching on untyped value" warnings; benign because `get` widened to Any in dbe8d81 |
| **D10** | methods_body.sf:1398 — `Task.spawn` `var task_result_type: String = "Int"` | Int | callee return type | Latent; probed with a String-returning task, works (resolved before fallback) |
| **D11** | methods_body.sf:2390-2397 — `to_string` final `else` | untag as Int | unknown | Latent |
| **D12** | methods_body.sf:2494 — `super` `else` | `StringType` | unknown | Latent, clearly a copy-paste artifact |
| **D13** | :771, :803, :835 (`gen_namespace_call`), :1174, :1308, :1347, :1362, :2595, :2634, :2649, :2748 | `IntType` | unknown | Bulk "I don't know" → Int. Should be `AnyType`. Latent |
| **D14** | :2009 `has`→`__map_has`, :2303 `to_number`→`atol`, :2314 `to_float`→`strtod`, :2077/:2093/:2111/:2126 String-only with no receiver guard | assumes receiver kind | — | Unguarded dispatch, not a *type* claim, but same failure shape |

### 1.5 The two live bugs (new, not in BUGS.md)

**Bug A — `Map.keys()`/`Map.values()` element type.** methods_body.sf:1771 and :1782 both call `this.make_list_str_type()` unconditionally, i.e. `List<String>`, for *any* map. Instrumentation shows this firing on real tests: `test/maps.sf` → `Map<String,Int>`, `test/test_stdlib.sf` → `Map<String,String>`, `test/toml_test.sf` → `Map<String,Any>`.

```saffron
var m: Map<String, Int> = {"a": 1}
var vs = m.values()
var t: Int = 0
for (v in vs) { t = t + v }   // SEGFAULT — elements treated as String pointers
IO.println(t.to_string())
```

Also segfaults for `Map<Int,String>.keys()` summed as Ints. Notably `IO.println(vs[0].to_string())` *works* — the corruption only surfaces when the false `String` element type drives a subsequent operation, which is exactly why it went unnoticed. Explicitly annotating `var vs: List<Int> = m.values()` works, confirming the inference site is the sole cause.

**Bug B — `slice` on an unknown receiver.** methods_body.sf:250-253: if the receiver type string does not start with `List`, `get_expr_type` returns `"String"`.

```saffron
fun mk(): Any { return [1,2,3] }
var a = mk()
var s = a.slice(0, 2)              // get_expr_type claims String
IO.println(s.length().to_string()) // prints 1, expected 2 — strlen on a list pointer
```

Direct chaining (`a.slice(0,2).length()`) is correct; only binding to a variable — which persists the false type in `typed_vars` — breaks it. Same shape as #36.

**Both fixed in prototype, 29 changed lines:**
- Added `get_map_key_type` next to `get_map_value_type` (types_body.sf:421), 7 lines.
- methods_body.sf:1771/:1782 → `make_generic_type("List", [str_to_type(get_map_key_type/get_map_value_type(obj_type))])`.
- methods_body.sf:250-253 → return `"String"` only when receiver is literally `String`, else `"Any"`.

Verified: `./bootstrap.sh` exit 0; all four repros correct; full suite **82 passed / 47 failed / 13 skipped — byte-identical to baseline** (modulo random tempdir names).

### 1.6 Duplication: four copies of builtin signature knowledge

1. `get_expr_type` method-call arm — methods_body.sf:249-282
2. The dispatch arms themselves — methods_body.sf:1643-2470
3. `class_methods` name lists — codegen.sf:163-168 (names only, no signatures)
4. **`checker.sf:1606-1655`** — receiver-aware, and it **disagrees**: `Map.get` returns nullable `V?` (checker.sf:1606-1616) while codegen returns bare `V` (methods_body.sf:2033-2076). `checker.sf:1621` also hardcodes `keys → List<String>` — the same Bug A, in the checker.

The disagreement, not the branch count, is the strongest structural argument for consolidation. But see §6: it has not yet been shown to cause a bug, and the nullable-vs-bare split may be intentional (checker enforces nil-safety; codegen only needs the storage shape).

---

## 2. Where signatures should live

### 2.1 Why `find_class_for_method` skips builtins — the half-registration clue, resolved

methods_body.sf:12-15:

```saffron
// Skip built-in types (String, List, Map, etc.) that are registered in
// class_methods but not in class_fields. Their methods are handled by
// builtin method dispatch.
if (!this.class_fields.has(class_name)) { i = i + 1; continue }
```

codegen.sf:163-168 registers builtin method **names** in `class_methods` with no `class_fields` entry and no signature payload. If `find_class_for_method` did *not* skip them, class dispatch would emit calls to nonexistent symbols like `@List__push`. The skip is correct, not a bug.

The clue's real content: **builtins are already half-registered — names without payload.** The refactor's substance is adding the payload (receiver kind, param types, return type, extern symbol, tagging discipline), not inventing a registry.

### 2.2 Assessment of the three options

**(a) Saffron stdlib `.sf` declarations.** Cleanest in principle: `class Map<K,V> { fun get(key: K): V ... }` in `src/lib/`. Rejected. `List`/`Map`/`String` are not heap objects with vtables; they are NaN-boxed values dispatched to C externs with per-method tagging rules (`__list_remove` takes a **raw** i64 index, methods_body.sf:1692-1694; `length` returns raw i64 then `emit_tag_int`). A `.sf` declaration cannot express "untag arg 0, call `@__list_remove`, tag result as Int". You would need an `extern`-with-tagging annotation syntax — new syntax, needing a gen2 promotion, for a problem worth 29 lines.

**(b) A compiler-side table.** The right shape, and **it already exists and was abandoned** (§1.2). The pipe encoding `"v2|__list_push|Nil|0"` is exactly this. Reviving it means re-litigating the decision that produced the current branches. The branches exist because the real behaviour is irregular: `starts_with` emits inline strlen+strncmp (:2077-2092), `contains` emits a **loop** for List/Map (:2224), `to_string` dispatches nine ways (:2323-2399). A table cannot encode those without a `special` escape hatch — at which point the table covers the easy half and the branches remain for the hard half, and you have two mechanisms instead of one. **This is precisely the failure mode already visible in the codebase.**

**(c) Reuse `class_methods`/`class_fields`/`class_struct_names`.** Actively harmful. Giving builtins `class_fields` entries removes the `find_class_for_method` guard and routes them to class dispatch, emitting undefined `@List__push` symbols. Would require a parallel "is builtin" flag — i.e. option (b) wearing a disguise.

**Recommendation: none of the three, for now.** Keep the branches. Add **one narrow shared helper** for the single genuinely-regular thing: element-type derivation. That is `get_map_key_type` + `get_map_value_type` + `get_list_element_type` — the fix in §1.5. This is the 80/20: three of four historical bugs and both new ones are element-type derivation errors, not dispatch-structure errors.

### 2.3 Do `get_map_value_type`/`str_to_type` suffice for generics?

**Yes, for everything currently needed.** Measured: `get_map_value_type` (types_body.sf:421) string-slices `Map<...>` using `split_respecting_generics`, correctly handling `Map<String,List<Int>>`. `str_to_type` (types_body.sf:34) round-trips back to `AST.Type`. My prototype's `List<K>`/`List<V>` derivation works via exactly this path, including nested generics.

Real substitution (a `K → concrete` binding environment) would be needed only for user-defined generic classes with methods mentioning multiple type params in non-trivial position. That is a **separate, larger** problem — note the heuristic already papering over it at methods_body.sf:2807-2821 ("looks like a type parameter" if length ≤ 2, all-uppercase, not IO/OS). Do not couple builtin dispatch to it.

---

## 3. Bootstrap constraint

**Measured conclusion: no gen2 promotion is required for any recommended step.**

I wrote a capability probe (`/tmp/gen2_probe.sf`), compiled it with `build/stage2/saffronc` (exit 0), and ran it end-to-end with fully correct output. It exercises everything a signature table or an element-type helper would need:

- `Map<String, Sig>` holding user-class instances
- `Map<String, List<String>>`
- a method returning a class instance
- pipe-encoded strings: `enc.set("push", "v2|__list_push|Nil|0")` + `.split("|")` + indexing
- list-in-map with `.length()` and indexing
- a string-based generic substitution helper (`fun subst(ret, k, v)` returning `k`/`v`/`ret`)

Output: `recv=Map ret=V` / `f0=v2 f2=Nil` / `n=3 first=push` / `subst=Int` — all correct.

Independently, both prototype fixes bootstrapped cleanly (`./bootstrap.sh` exit 0) using `make_generic_type`, `str_to_type`, and a new method on the codegen class — all gen2-parseable.

**The "Unified Dispatch" project note is stale.** Its claim that this work "Depends on: gen2 promotion fix (signal 137 crash)" no longer holds; three bootstraps of modified compiler source completed cleanly. CLAUDE.md's only remaining documented gen2 limitation is **tuple-literal syntax in compiler source**, which none of this needs (use lists).

**Sequencing risk: low.** The one thing that *would* need care is adding new syntax (e.g. an `extern`-with-tagging annotation for option (a)) — another reason to reject option (a).

---

## 4. Staging

Four steps, each independently shippable and revertable. Steps 1-3 are the recommendation; step 4 is conditional.

### Step 1 — Delete dead `builtin_methods` machinery
**Change:** remove methods_body.sf:552-674 (`set_result_type`, `tag_builtin_result`, `emit_builtin_dispatch`) and the two dead guards at :1923-1960. Remove `codegen.sf:59` and `:125`. ~123 lines + 2 branches.
**Verify:** `./bootstrap.sh` in an isolated tree; full suite must stay 82/47/13.
**Regression risk:** near zero — no writer exists, proven by grep. Watch only for the parallel `methods.sf` copy referencing the deleted helpers; it is stripped from the build, but keep the two files consistent to avoid confusing future readers.

### Step 2 — Harden the terminal fall-through (BUGS #37's fix)
**Change:** methods_body.sf:2860-2893. Replace the unconditional
```saffron
this.last_type = AST.Type.IntType
return "0"
```
with a real error:
```saffron
IO.println("[codegen] Error: no method '" + method + "' on type '" + obj_type + "' (receiver type could not be resolved)")
this.has_errors = true
this.last_type = AST.Type.AnyType
return "0"
```
**Measured result — already done in prototype:** bootstrap exit 0, **0 errors emitted while compiling the compiler itself**, suite stays **82/47/13**. Exactly **one** outcome changed, and it improved:
```
- FAIL invalid-ir    json — output.ll:483:25: use of undefined value '%Json'
+ FAIL compile-error json — [codegen] Error: no method 'parse' on type 'Int'
```
`test/json.sf` was already failing; it now fails *honestly at compile time* instead of emitting broken IR. Root cause is upstream (`import "@json" as Json` at json.sf:1 is not resolving — codegen also emits "undefined variable 'Json'"), i.e. a real module-resolution bug this change **surfaces rather than causes**.
**Regression risk:** low but real — this converts any *future* unresolved receiver from silent-wrong-code into a build failure. That is the point. Ship it separately from step 3 so a bisect is unambiguous.

### Step 3 — Fix the live dishonest fallbacks
**Change (verified, 29 lines):**
- add `get_map_key_type` to types_body.sf beside `get_map_value_type`
- methods_body.sf:1771/:1782 — derive `List<K>`/`List<V>` from the receiver (**Bug A**)
- methods_body.sf:250-253 — `slice` returns `String` only for a literal `String` receiver, else `Any` (**Bug B**)
- `checker.sf:1621` — same `keys → List<String>` bug; fix with `extract_map_value_type`'s key counterpart

**Then, in a follow-up commit,** the latent ones (D6, D7, D8, D11, D12, D13): mechanically replace "I don't know → `IntType`" with `AnyType`. Each is one line. Do these *after* step 2 so the hardened terminal can catch anything the widening exposes.
**Verify:** bootstrap; suite unchanged; the four repros in §1.5; new regression test (§5).
**Regression risk:** widening to `Any` routes through `__any_length`/`__any_to_string`, which is *slower* but correct. Watch for perf-sensitive paths and for the "dispatching on untyped value" warning count growing (it is a useful signal, not noise — do not silence it).

### Step 4 — Unify the four signature tables — **CONDITIONAL, recommend deferring**
Only if the codegen/checker `Map.get` nullability disagreement is shown to cause a real bug. It has not been. Deferring costs one duplicated `keys` fix in step 3. See §6.4.

**Note on `docs/design/builtin-module-dispatch.md`:** that document proposes the table approach for the **IO/OS/GC module if-chains** (`"IO.println" → {func, ret, special}`), not container methods. Module namespace calls *are* regular — fixed arity, no receiver polymorphism, no tagging irregularity. **That plan is still good and is orthogonal to this one.** Keep them separate; don't let this analysis be read as rejecting it.

---

## 5. Verification strategy

### 5.1 Isolated tree (mandatory)

Never run `./bootstrap.sh` in the working repo — it corrupts the shared `build/`.

```bash
rm -rf /tmp/sfrefac && mkdir -p /tmp/sfrefac
git archive HEAD | tar -x -C /tmp/sfrefac
cp tools/run_tests.sh /tmp/sfrefac/tools/run_tests.sh   # SEE BELOW
cd /tmp/sfrefac && ./bootstrap.sh     # 3-8 min, use timeout 540000
```

**Gotcha that cost me an hour, document it:** `tools/run_tests.sh` currently has **uncommitted** improvements, so `git archive HEAD` ships a stale 54-line version that links `base.ll` instead of `base_nanbox.ll` and reports a bogus "0 passed, 105 failed" — the exact failure mode its own header comment warns about. Copy the working runner in before baselining. (Copying *out* is read-only on the real repo, so it respects the no-stash/no-checkout rule.)

### 5.2 Baseline and comparison

Baseline: **82 passed, 47 failed, 13 skipped** over 142 files. Compare by summary line *and* by per-test diff, normalising random tempdir names:

```bash
diff <(grep -E "^(PASS|FAIL|SKIP)" baseline.log | sed 's/saffron_build_[A-Za-z0-9]*/T/') \
     <(grep -E "^(PASS|FAIL|SKIP)" candidate.log | sed 's/saffron_build_[A-Za-z0-9]*/T/')
```

Without the `sed`, every run "differs" and you will chase ghosts. I did.

### 5.3 `test/fail/*.sf` — the suite is too weak to protect you

Exactly 5 files: `class_errors.sf`, `conformance.sf`, `exhaustiveness.sf`, `inheritance_errors.sf`, `type_errors.sf`. Measured:
- `conformance` / `type_errors` catch interface non-implementation
- `class_errors` catches a parse error
- **`exhaustiveness` and `inheritance_errors` produce no compiler error at all** — they are 2 of the 47 baseline failures (`not-rejected`)
- **none of the 5 exercises method dispatch**

So widening to `Any` has almost no fail-suite exposure — low regression risk, **but the suite cannot detect a soundness loss either.** Do not treat "fail suite unchanged" as evidence of preserved rejection power. It is evidence of nothing.

Worse, I measured that the checker already fails to reject basic annotation mismatches:
```saffron
var s: String = 42          // exit 0 — not rejected
var l: List<Int> = ["a"]    // exit 0 — not rejected
```
The type system is weaker *today* than the fail suite implies. That reframes §6 substantially.

### 5.4 Regression test to add

Model on `test/test_map_get_types.sf` (from dbe8d81) — it pins **both** directions, which is what makes it valuable: annotated `Map<K,V>` must narrow, unannotated `Map()` must yield `Any` and still dispatch at runtime. Extend it (or add `test/test_map_keys_values_types.sf`) with:

```saffron
// narrowing direction
var mi: Map<String, Int> = {"a": 1, "b": 2}
var total: Int = 0
for (v in mi.values()) { total = total + v }
T.assert_eq(total, 3, "Map<String,Int>.values() yields Int elements")

var mk: Map<Int, String> = {}
mk.set(7, "x")
var ksum: Int = 0
for (k in mk.keys()) { ksum = ksum + k }
T.assert_eq(ksum, 7, "Map<Int,String>.keys() yields Int elements")

// Any direction — must not crash, must runtime-dispatch
fun mkany(): Any { return [1, 2, 3] }
var a = mkany()
var s = a.slice(0, 2)
T.assert_eq(s.length(), 2, "slice on Any receiver keeps list semantics")
```

Every one of these fails on the current committed compiler (the first two segfault). That is the test that would have caught both new bugs.

### 5.5 Where instrumentation pays off

`IO.println("[DBG] ...")` in codegen was decisive and **provably non-perturbing** (instrumented run diffed byte-identical to baseline apart from tempdir names). Highest value, in order:

1. **The terminal fall-through** (methods_body.sf:2860) — the single most informative probe. Zero hits is what killed the refactor case.
2. **Any branch about to assert a concrete type in an `else`** — print the receiver type string. This is what found both live bugs: `[DBG-MAPVALUES] obj_type=[Map<String,Int>]` is self-evidently wrong.
3. **`length`'s `else`** (:2216) — printed `List<Any>`, `List<Int>`, `List<String>` across the suite: currently all legitimate, so #32 is genuinely closed, but the probe re-confirms it cheaply.

Practical note: `tools/run_tests.sh` swallows compiler stdout, so suite-wide DBG counts read as zero. Compile representative files directly (`./build/saffronc test/maps.sf /tmp/o.ll`) to actually see hits. I nearly drew a false "zero hits" conclusion from the suite log alone.

---

## 6. Honest risk assessment

### 6.1 The concern, stated precisely

"Excessive uses of `Any` may compromise the type system." Valid in general. Two distinct things are conflated:

- **Codegen `last_type`** — a *dispatch hint* answering "which extern do I emit and how do I tag?" `Any` here means "emit a runtime-dispatching call" (`__any_length`, `__any_to_string`). Honest and safe.
- **Checker types** — the *soundness contract*, answering "should this program be rejected?" `Any` here means "stop checking", which does erode rejection power.

**These are different passes over different data.** `checker.sf` (2355 lines) is wired at `main.sf:9` and invoked at `main.sf:991-1000`, before codegen. Widening a codegen fallback from `IntType` to `AnyType` **cannot** change what the checker rejects.

### 6.2 Where the tension actually bites

Three places, all manageable:

1. **The two passes share helper names and shapes** (`get_map_value_type` vs `extract_map_value_type`) and both hardcode `keys → List<String>`. It is easy to "fix the fallback" in the wrong file, or to unify them and accidentally let codegen's permissive `Any` flow into checker decisions. **Mitigation: fix them in separate commits and never let codegen call a checker helper or vice versa.** If step 4 ever happens, the shared table must be *data* consumed independently by both, never a shared decision procedure.

2. **`Map.get` nullability.** Checker says `V?` (checker.sf:1606-1616, with a `__has_guard` escape); codegen says `V`. Unifying naively would either make codegen emit nullable-aware code it does not need, or make the checker stop demanding nil checks — **the latter is a genuine soundness regression.** This is the single strongest reason to *defer* step 4.

3. **Any widening interacts with `typed_vars` persistence.** Both new bugs came from a false type being *stored* in `typed_vars` and reused later. Widening to `Any` is right, but it means more variables carry `Any`, so more downstream dispatch goes through the runtime path. Correct, and slower. If a hot path regresses, the fix is better *forward* inference, not re-narrowing with a guess.

### 6.3 Where the refactor could make things weaker

- **Option (a) (stdlib `.sf` declarations) is the dangerous one.** Declaring `class Map<K,V>` in Saffron makes the checker treat maps as user classes, activating conformance/inheritance machinery that has never run on them (and note `fail/inheritance_errors.sf` currently rejects nothing). Unknown-unknowns; avoid.
- **Deleting fallback branches too early.** The staging discipline (route through new path, keep old branch, delete only once instrumentation proves it dead) matters *because* the fail suite cannot catch a mistake here (§5.3).
- **Hardening the terminal is the one change that could break a working build.** Measured impact today: one already-failing test fails differently and more honestly. Accept it; ship it alone.

### 6.4 Is the refactor worth it? No.

**Cost of the refactor as framed:** the builtin dispatch region is methods_body.sf:1614-2470 = **857 lines**; `gen_method_call` is :854-2894 ≈ **2040 lines** with ~50 branch sites; plus `get_expr_type`'s 34-line parallel copy, `class_methods` registration, and `checker.sf:1560-1660`. Realistically **~1000 lines rewritten across 4 files**, with an irregularity escape hatch for `starts_with`/`contains`/`to_string`, verified only by a suite with 47 pre-existing failures and a fail suite that tests no dispatch at all. Multi-week, high-variance.

**Cost of the targeted fix:** **29 lines, measured**, two real bugs eliminated, bootstrap clean, suite identical.

**The decisive arguments:**

1. **The bug class the refactor targets is already closed.** Zero terminal fall-throughs compiling the compiler and the entire suite. The four historical bugs are fixed. The refactor would prevent a *fifth* instance of a mechanism that no longer fires — and step 2 prevents that for ~6 lines.
2. **The refactor was already attempted and abandoned.** `emit_builtin_dispatch` (:576-674) is a complete table-driven dispatcher, bypassed by hand-written branches whose comments say "replaces builtin_methods for List/Map/String". Someone built the thing being proposed and preferred the branches. Absent an explanation of why they were wrong, repeating it is expensive.
3. **The branch count is not the defect; the element-type derivation is.** #32, #36, #37 and both new bugs are all "wrong element/result type", none is "wrong dispatch structure". A one-path dispatcher with the same wrong element-type derivation ships the same bugs, just centralised. Conversely, the 29-line fix removes both bugs *while keeping* 40 branches.
4. **The verification substrate is too weak to land a 1000-line rewrite safely.** 47 baseline failures, a 5-file fail suite with 2 files rejecting nothing, and a checker that accepts `var s: String = 42`. You cannot distinguish "refactor is correct" from "refactor broke something the suite never covered." **Strengthening tests must precede any large refactor here** — and once the tests are strong enough, the incremental fixes are cheap enough that the refactor's marginal value shrinks further.

**What would change my answer:** if the codegen/checker `Map.get` nullability split produces a real shipped bug, or if a *fifth* dishonest-fallback bug appears in a site the 29-line fix did not touch, revisit — but scoped to a shared element-type/signature **data table** consumed independently by both passes (option (b) as data, not as a dispatcher), and only after the fail suite covers dispatch.

### 6.5 Recommended plan and effort

| Step | Change | Lines | Effort | Risk |
|---|---|---|---|---|
| 1 | Delete dead `builtin_methods` machinery | −125 | 30 min | ~zero |
| 2 | Harden terminal fall-through | ~6 | 1 h (incl. bootstrap) | low, measured |
| 3a | Fix live fallbacks D1, D2 (+ checker `keys`) | ~29 | 1 h | low, measured |
| 3b | Widen latent D6-D8, D11-D13 to `Any` | ~15 | 2 h | low |
| 3c | Add bidirectional regression test | ~30 | 1 h | none |
| — | *(deferred)* unify signature tables | ~1000 | weeks | high |

**Total for the recommendation: ~1-2 days, four commits, each independently verifiable and revertable.** Steps 1-3a are already prototyped and measured green in `/tmp/sfrefac`.

Two side findings worth separate tickets, surfaced by this work but not part of it:
- `test/json.sf`: `import "@json" as Json` is not resolving (codegen warns "undefined variable 'Json'"), producing invalid IR today and a hard error after step 2.
- The checker accepts `var s: String = 42` and `var l: List<Int> = ["a"]`. Independent of dispatch, and a bigger soundness hole than anything in this document.
