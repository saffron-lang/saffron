# Copy

```saffron
import "@copy" as Copy
```

Shallow and deep copying of values.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Copy.shallow(value)` | `Any` | A one-level copy: new List/Map with the same elements; primitives/strings unchanged |
| `Copy.deep(value)` | `Any` | A recursive copy sharing no mutable structure; handles cyclic List/Map graphs |

## What copies, and how

- **Primitives** (`Int`, `Float`, `Bool`, `Nil`) and **`String`** are immutable
  values — both `shallow` and `deep` return them unchanged.
- **`List`** and **`Map`** are copied: `shallow` makes a new container holding the
  same elements; `deep` recurses into every element.
- **Class instances are opt-in.** An instance is copied only if its class
  `extends Cloneable`; otherwise `Copy.deep`/`Copy.shallow` throw rather than
  silently share it.

## Cycles

`Copy.deep` handles cyclic **List/Map** graphs (`a.push(a)`): a value already
being copied is recorded and the same copy reused, so the result mirrors the
input's sharing instead of looping forever. Cycles *through a class instance* are
that instance's own `clone()` to handle.

## Example: deep vs shallow

```saffron
import "@copy" as Copy

var a = [[1, 2], [3, 4]]
var b = Copy.deep(a)
b[0].push(99)          // a[0] is unchanged — the nested list was copied

var s = Copy.shallow({"k": [1, 2]})
// the map is new, but the [1, 2] list is shared with the original
```

## Making a class copyable

Import the `Cloneable` interface **by name** (a module qualifier is not accepted
after `extends`) and implement `clone()`:

```saffron
import "@copy" as Copy
import { Cloneable } from "@copy"

class Box extends Cloneable {
    var v: Int
    fun init(v: Int) { this.v = v }
    fun clone(): Any { return Box(this.v) }
}

var original = Box(5)
var copy = Copy.deep(original)
copy.v = 99
// original.v is still 5 — clone() produced an independent instance
```

`clone()` is the whole contract: `@copy` never reaches inside an instance, so the
author decides how deep a clone goes and how to treat any resource it owns (an
fd, a socket, a cache — reopen, share, or drop). This is the Rust/Java stance
(`derive(Clone)` / `implements Cloneable`), not Python's copy-anything default:
copying a handle by memory is almost never what you want, so the language makes
you say so.
