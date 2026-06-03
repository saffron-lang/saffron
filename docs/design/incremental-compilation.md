# Incremental Compilation

## Status: Proposal
## Date: 2026-06-03

---

## 1. Overview

Saffron's compiler (`saffronc`) currently performs a full recompilation on every invocation, producing a single bundled `.ll` file. For small-to-medium projects this takes 2-4 seconds — acceptable for development, but unnecessary when nothing has changed.

This design introduces a cache layer that skips recompilation entirely when inputs haven't changed, reducing "no changes" rebuilds to under 100ms.

---

## 2. Architecture

### Approach: whole-bundle caching

Rather than per-file incremental compilation (which requires interface files, declare/define separation, and major codegen refactoring), we cache the entire compiler output:

```
[inputs] → hash → cache lookup → hit? → skip saffronc, link from cache
                                → miss? → full recompile, store result
```

This is simple, correct, and captures 90% of the benefit for iterative development (most rebuilds during `pantry test` loops change nothing).

---

## 3. Cache Location

| Context | Cache directory |
|---------|----------------|
| Pantry project | `.pantry/cache/` |
| Standalone (no pantry.toml) | `build/.cache/` |

Cache contents:

```
.pantry/cache/
├── manifest.json       ← tracks all input files + hashes
├── output.ll           ← cached compiler output
└── output.ll.hash      ← hash of the cached .ll (for verification)
```

---

## 4. Cache Key

The cache key is a composite hash of all compilation inputs:

```
cache_key = sha256(
    compiler_binary_hash +
    stdlib_files_hash +
    source_files_hash +
    build_flags_string
)
```

### Input components

| Component | What's hashed | Why |
|-----------|---------------|-----|
| Compiler binary | `sha256(build/stage2/saffronc)` or `sha256(build/saffronc)` | Compiler changes produce different output |
| Stdlib files | Combined hash of all `src/lib/*.sf` | Stdlib is bundled into output |
| Source files | Combined hash of all `.sf` files in project | Any source change invalidates |
| Build flags | Sorted string of `--lib-path`, `--stdlib`, etc. | Different flags = different output |

---

## 5. Manifest File

The manifest tracks file paths, modification times, and content hashes:

```json
{
  "cache_key": "a1b2c3...",
  "compiler": "build/saffronc",
  "compiler_hash": "f4e5d6...",
  "flags": "--stdlib src/lib --lib-path .pantry/packages",
  "files": {
    "src/main.sf": { "mtime": 1717420800, "hash": "abc123..." },
    "src/parser.sf": { "mtime": 1717420700, "hash": "def456..." },
    "src/lib/iter.sf": { "mtime": 1717400000, "hash": "789abc..." }
  }
}
```

---

## 6. Invalidation Strategy

### Fast path: mtime pre-check

Before computing any content hashes, check modification times:

1. Read existing `manifest.json`
2. For each tracked file, compare current `mtime` to manifest `mtime`
3. If ALL mtimes match → **cache hit** (skip hashing entirely)
4. If any mtime differs → compute content hash for changed files
5. If content hash matches despite mtime change (e.g., `touch` with no edit) → still a hit
6. Otherwise → **cache miss**, full recompile

### Invalidation triggers

Any of these cause a cache miss:
- Source file added, removed, or modified
- Stdlib file changed
- Compiler binary changed (new bootstrap)
- Build flags changed (new `--lib-path`, different `--stdlib`)
- Manifest missing or corrupt

---

## 7. Implementation

### Location: `tools/saffron` driver script

The caching logic lives in the shell driver (~50 lines of bash), wrapping the existing `saffronc` invocation:

```bash
# Pseudocode for cache check in tools/saffron
CACHE_DIR=".pantry/cache"
MANIFEST="$CACHE_DIR/manifest.json"

# Compute current cache key
current_key=$(compute_cache_key "$COMPILER" "$STDLIB" "$SOURCES" "$FLAGS")

# Check manifest
if [ -f "$MANIFEST" ]; then
    cached_key=$(jq -r '.cache_key' "$MANIFEST")
    if [ "$current_key" = "$cached_key" ] && mtimes_match "$MANIFEST"; then
        # Cache hit — skip compilation
        cp "$CACHE_DIR/output.ll" "$OUTPUT"
        exit 0
    fi
fi

# Cache miss — full recompile
"$COMPILER" $FLAGS "$INPUT" "$OUTPUT"

# Store result
cp "$OUTPUT" "$CACHE_DIR/output.ll"
write_manifest "$MANIFEST" "$current_key" "$SOURCES"
```

### `compute_cache_key` function

```bash
compute_cache_key() {
    local compiler="$1" stdlib_dir="$2" source_dir="$3" flags="$4"
    (
        sha256sum "$compiler"
        find "$stdlib_dir" -name "*.sf" -exec sha256sum {} \;
        find "$source_dir" -name "*.sf" -exec sha256sum {} \;
        echo "$flags"
    ) | sha256sum | cut -d' ' -f1
}
```

### mtime fast path

```bash
mtimes_match() {
    local manifest="$1"
    # Extract file list and mtimes from manifest, compare against filesystem
    jq -r '.files | to_entries[] | "\(.key) \(.value.mtime)"' "$manifest" |
    while read -r path expected_mtime; do
        actual_mtime=$(stat -f %m "$path" 2>/dev/null || echo "missing")
        if [ "$actual_mtime" != "$expected_mtime" ]; then
            return 1
        fi
    done
}
```

---

## 8. Performance

| Scenario | Without cache | With cache |
|----------|--------------|------------|
| No changes | 2-4s (full recompile) | <100ms (mtime check + copy) |
| Single file changed | 2-4s | 2-4s (cache miss, full recompile) |
| New dependency added | 2-4s | 2-4s (flags change, cache miss) |
| `--no-cache` flag | 2-4s | 2-4s (bypass) |

The primary win is the "edit test loop" where you change a test file, run tests, see failure, fix, run again — the second compilation of unchanged library code is free.

---

## 9. Bypass and Cleanup

### `--no-cache` flag

```bash
pantry build --no-cache
tools/saffron run program.sf --no-cache
```

Skips both cache lookup and cache storage.

### `pantry clean`

Already removes `.pantry/` which includes `.pantry/cache/`. No additional work needed.

### Manual cache clear

```bash
rm -rf .pantry/cache/
```

---

## 10. Future: Phase 2 (Per-File Compilation)

True incremental compilation — where only changed files are recompiled — requires:

1. **Interface files**: each module emits a `.sfi` file describing its public API
2. **Declare/define separation**: codegen must emit `declare` for imported symbols and `define` only for the current file
3. **Per-file `.ll` output**: each source file produces its own `.ll`
4. **Dependency graph**: track which files import which, recompile only downstream of changes
5. **Linking step**: `llvm-link` merges per-file `.ll` into the final bundle

This is a major codegen refactoring effort (~weeks of work) and is deferred. The whole-bundle cache in Phase 1 provides the critical "no changes = instant" optimization with minimal implementation cost.

---

## 11. Open Questions

1. **Cache sharing in CI?** Could upload/download `.pantry/cache/` as a CI artifact. Requires stable cache keys across machines (normalize paths).
2. **Multiple entry points?** If a project has multiple binaries, cache per entry point: `.pantry/cache/<entry-hash>/`.
3. **Parallel workspace caching?** In a workspace, each member gets its own cache. Members built in parallel can populate caches concurrently.
4. **Cache size limits?** A single `.ll` file is typically 10-200KB. No eviction needed for Phase 1.
