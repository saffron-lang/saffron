# RESUME: I3 enum-fields migration — Step 2 (unverified) + Steps 3–4

**Read this file, then follow "How to pick up" below.** This is the resume point
for the I3 (Stage-3 type-representation) enum-fields slice. Written 2026-08-11.

## One-paragraph context

We are migrating enum `Variant.fields` from a CSV string (`"name:Type,other:Type2"`)
to `List<AST.Param>`, in four bootstrappable steps, mirroring the proven `func_params`
(commit `35fdadd`) / `class_fields` (`9a0ec5e`) template. The full plan lives in
`docs/design/compiler-rewrite.md` under **I3** (search "sixth slice — enum
`Variant.fields`"). The tactical antipattern entry is `docs/design/codegen-refactors.md` §6.
The **critical correctness invariant**: enum fields store the loss-less parser source
form, so all rendering MUST use `AST.type_to_source`, NOT `type_to_string` (which would
collapse `Map<String,Int>` → `Map`).

## Status of each step

| Step | What | State | Where |
|---|---|---|---|
| 1 | `ast.sf` node → `List<Param>` + parser + `variant_fields_string` render shim; all readers routed through shim | ✅ LANDED, verified (gen4 + suite 369/0 + oracle 0 mismatch) | on `main`, commit `a77ffa2d` |
| 2 | codegen reads nodes directly (`enum_variant_fields` table → `Map<String,List<AST.Param>>`; `get_variant_field_type` + `ensure_enum_eq` read `type_ann`); removes `record_unresolved` site `match_body.sf:638` | ✅ LANDED, verified (gen4 + suite 369/0 + oracle 0 mismatch) | on `main` |
| 3 | migrate checker `enum_fields` table to nodes (`get_enum_binding_type`/`_node` read `type_ann`) | ❌ NOT STARTED | — |
| 4 | delete both render shims + dead `split_respecting_generics`/`split(":")` on enum strings | ❌ NOT STARTED | — |

## How to pick up (exact commands)

Step 2's edits are on a pushed WIP branch. To resume:

```bash
cd /Users/willemhs/personal/saffron
git fetch origin
# Option A — check out the WIP branch directly:
git worktree add /tmp/sf-i3s2 origin/wip/i3-step2-enum-nodes
# Option B — if that branch is gone, re-apply the patch onto current origin/main:
#   git worktree add -b i3-step2 /tmp/sf-i3s2 origin/main
#   cd /tmp/sf-i3s2 && git apply --3way /path/to/.claude/i3_step2.patch
```

Step 2 touches 5 codegen files only: `codegen.sf`, `codegen/{expr,match,output,stmts}_body.sf`.
No checker/ast/parser changes (those are Step 3).

## FIRST ACTION on resume: VERIFY Step 2 (it has never been built)

Under the single-flight heavy lock (see below), in the Step-2 worktree, run in order:
1. `./bootstrap.sh` — must reach "STAGE 2 … 0 unresolved inference fallbacks" and
   "gen3 compiles itself; gen4 links and compiles", then "Bootstrap complete!".
2. `tools/run_tests.sh` — must be `TOTAL: … 0 failed` (369 passed at time of writing;
   the number only grows as colleagues add tests).
3. `tools/differential.sh --suite oracle` — must be `MISMATCHES 0`.

**The one thing the oracle is verifying here:** Fun-typed enum payload fields
(`Fun(A):R`). Step 2's `get_variant_field_type`/`declared_field_type_node` derive the
field type by taking `AST.type_to_source(type_ann)` and truncating at the first `:`
(reproducing the old `split(":")[1]` behavior). Every other type shape has no `:` in
its source so passes through whole; `Fun(A):R` is the only shape where the truncation
point matters. If the oracle flags a mismatch, that's where to look.

If all three are green: commit Step 2 properly to `main` (source + regenerated
`build/stage3/*` artifacts), update the I3 slice in `compiler-rewrite.md` to mark
Step 2 LANDED, delete the WIP branch (`git push origin :wip/i3-step2-enum-nodes`) and
`.claude/i3_step2.patch`. Then proceed to Step 3.

## Steps 3 & 4 (after Step 2 lands green)

- **Step 3 (checker):** flip `checker.sf` `enum_fields: Map<String,String>` →
  `Map<String, List<AST.Param>>`; store nodes in `register_enum`; migrate
  `get_enum_binding_type` (~`checker.sf:5143`) and `get_enum_binding_type_node`
  (~`:5158`) to read `type_ann` directly instead of `parse_type_node(slice-at-colon)`.
  Keep `AST.variant_fields_string` alive until Step 4. Byte-identical; gate on
  bootstrap+suite+oracle. **Do NOT fold in** the bare-name `enum_variants`/`enum_fields`
  key-qualification change (`checker.sf` ~1836) — it's a separate, larger slice (~10
  read sites).
- **Step 4 (cleanup):** once no consumer renders a string, delete
  `AST.variant_fields_string`, the codegen `get_variant_field_str` shim, and the dead
  `split_respecting_generics`/`split(":")`/`split_type_args` calls on enum-field
  strings. Final suite + oracle.

## Mandatory operational constraints (do not skip)

- **Single-flight heavy jobs.** NEVER run bootstrap/suite/large compiles concurrently —
  the machine OOMs. Acquire the shared lock first. Pattern used this session: a wrapper
  that `mkdir /tmp/saffron-heavy.lock` (fails if held), `cd`s into the target worktree,
  runs the command, and `rm -rf` the lock on EXIT trap. One heavy job at a time, period.
- **Work in an isolated git worktree off `origin/main`, never in the shared main tree** —
  a colleague has uncommitted work there intermittently. Never `git stash`/`checkout`
  their files. Commit only your own named files (never `git add -A`).
- **Re-fetch `origin/main` before every commit/push** — it moves often (colleagues land
  fixes). Rebase; use 3-way `git apply` if a file you edited also moved upstream.
- **Bug/plan state is derived, not stored:** `tools/bugs.sh` for the open set. The
  `--report-unresolved` count on the compiler must stay 0 (bootstrap STAGE 2 enforces it);
  removing the `:638` site drops the *reported* set but the gate counts inference gaps,
  so it stays 0 either way.

## Related design lessons (memory) worth reading first
- The `Int`-fallback is sometimes load-bearing (tags a raw i64) — don't blanket-widen to
  `Any`; the honest end-state is error-on-truly-unknown, which needs I4 (elaborate) to
  distinguish "genuinely unknown" from "lookup missed". (This is why Steps 3–4 don't try
  to flip `record_unresolved`→hard-error yet.)
- The runtime `values.spec` generator is honest+gated now (`--check` in `run_tests.sh`);
  `.ll` bases are the source of truth if they ever disagree with the spec.
