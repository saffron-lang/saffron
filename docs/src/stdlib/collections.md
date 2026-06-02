# Collections

The collections chapter covers additional data structures beyond the built-in List and Map.

## Set

See the dedicated [Set](./set.md) module (`import "@set" as Sets`).

## Queue, Stack, Deque

These data structures can be implemented using List methods:

### Queue (FIFO)

```saffron
// Use a list as a queue (push to back, access front via index)
var q: List<String> = []
q.push("first")         // enqueue
q.push("second")
var front = q[0]        // peek front
```

### Stack (LIFO)

```saffron
// Lists are natural stacks
var s: List<String> = []
s.push("bottom")
s.push("top")
var top = s.pop()  // "top"
```

Formal Queue, Stack, and Deque classes may be added as a dedicated `@collections` module in a future release. For now, use List with `push`/`pop` for stack behavior.
