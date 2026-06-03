# `pantry new` Command

## Status: Proposal
## Date: 2026-06-03

---

## 1. Overview

`pantry new <name>` scaffolds a new Saffron project with sensible defaults: a `pantry.toml`, entry file, test file, `.gitignore`, and optionally a git repository. This removes the boilerplate of starting a new project.

---

## 2. CLI Interface

```
pantry new <name> [options]

Options:
  --lib            Create a library project (src/lib.sf instead of src/main.sf)
  --no-git         Skip git init and .gitignore generation
  --template=<t>   Use a project template (default, web, cli)
```

### Name Validation

Project names must:
- Contain only alphanumeric characters, hyphens (`-`), and underscores (`_`)
- Start with a letter
- Not be a reserved name: `test`, `src`, `build`, `pantry`, `saffron`

Invalid names produce a clear error:

```
error: invalid project name 'my project'
  names must be alphanumeric with hyphens or underscores
```

---

## 3. Generated File Layout

### Application project (default)

```
my-app/
├── pantry.toml
├── src/
│   └── main.sf
├── test/
│   └── test_main.sf
├── .gitignore
└── README.md
```

### Library project (`--lib`)

```
my-lib/
├── pantry.toml
├── src/
│   └── lib.sf
├── test/
│   └── test_lib.sf
├── .gitignore
└── README.md
```

---

## 4. Generated File Contents

### `pantry.toml`

```toml
[package]
name = "my-app"
version = "0.1.0"
entry = "src/main.sf"

[dependencies]
```

For libraries, `entry = "src/lib.sf"`.

### `src/main.sf`

```saffron
import "io" as IO

fun main() {
    IO.println("Hello from my-app!")
}

main()
```

### `src/lib.sf`

```saffron
// my-lib: a Saffron library

fun hello(): String {
    return "Hello from my-lib!"
}
```

### `test/test_main.sf`

```saffron
import "@test" as T
import "../src/main.sf" as Main

T.assert(true, "placeholder test")
T.summary()
```

### `test/test_lib.sf`

```saffron
import "@test" as T
import "../src/lib.sf" as Lib

T.assert_eq(Lib.hello(), "Hello from my-lib!", "hello returns greeting")
T.summary()
```

### `.gitignore`

```
build/
.pantry/
*.ll
*.o
```

### `README.md`

```markdown
# my-app

A Saffron project.

## Build

\```bash
pantry build
\```

## Test

\```bash
pantry test
\```
```

---

## 5. Templates

### `--template=default`

Standard layout as shown above. This is the implicit template when no flag is given.

### `--template=web`

Adds turmeric as a dependency and generates a minimal web app entry point:

```toml
[dependencies]
turmeric = { git = "https://github.com/user/turmeric.git" }
```

```saffron
import "@turmeric" as UI

var app = UI.App()
app.mount("#root", fun () {
    return UI.h("div", {}, ["Hello, web!"])
})
```

### `--template=cli`

Generates a CLI entry point with argument parsing:

```saffron
import "io" as IO
import "os" as OS

fun main() {
    var args = OS.args()
    if (args.length() < 1) {
        IO.println("Usage: my-cli <command>")
        return
    }
    IO.println("Running: ${args[0]}")
}

main()
```

---

## 6. Workspace Awareness

When `pantry new` is run inside a directory that contains (or is nested within) a workspace root, it prints a helpful note:

```
  Created project 'my-app' in ./my-app

  note: workspace detected at ../pantry.toml
  To add this package to the workspace, add "my-app" to [workspace].members
```

This is informational only — it does not modify the workspace `pantry.toml`.

---

## 7. Success Output

```
$ pantry new my-app

  Created project 'my-app' in ./my-app

  Next steps:
    cd my-app
    pantry build
    pantry test
```

For `--no-git`:

```
$ pantry new my-app --no-git

  Created project 'my-app' in ./my-app (no git)

  Next steps:
    cd my-app
    pantry build
    pantry test
```

---

## 8. Implementation

### New file: `pantry/src/commands/new.sf`

```saffron
import "io" as IO
import "@fs" as FS

class NewCommand {
    var name: String
    var is_lib: Bool
    var no_git: Bool
    var template: String

    fun init(name: String, is_lib: Bool, no_git: Bool, template: String) {
        this.name = name
        this.is_lib = is_lib
        this.no_git = no_git
        this.template = template
    }

    fun validate_name(): Bool { ... }
    fun run() { ... }
    fun generate_manifest(): String { ... }
    fun generate_entry(): String { ... }
    fun generate_test(): String { ... }
    fun generate_gitignore(): String { ... }
    fun detect_workspace(): String? { ... }
}
```

### Changes to existing files

| File | Change |
|------|--------|
| `pantry/src/main.sf` | Add `"new"` to command dispatch, parse `--lib`, `--no-git`, `--template` flags |
| `pantry/src/config.sf` | Add `RESERVED_NAMES` constant for validation |

### Implementation notes

- Use `FS.mkdir_p()` for directory creation
- Use `FS.write()` for file creation
- Git init: shell out to `git init` + `git add .` via `OS.exec()`
- Template selection is a simple match on the `--template` value
- Total implementation: ~150 lines

---

## 9. Open Questions

1. **Interactive mode?** Could prompt for name/type if no args given. Defer to later.
2. **Custom templates?** Allow `--template=<path>` for user-defined templates in a future version.
3. **License selection?** Could add `--license=MIT` flag. Low priority.
4. **Edition field?** If Saffron introduces editions, `pantry new` should default to the latest.
