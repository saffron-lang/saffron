# Collections

```saffron
import "@collections" as Collectionsollections
```

Additional data structures beyond the built-in List and Map.

## Set

An unordered collection of unique values.

### Creating sets

```saffron
import "@collections" as Collections

var s = Collections.Set.new()
var from_list = Collections.Set.from([1, 2, 3, 2, 1])  // {1, 2, 3}
```

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `s.add(item)` | — | Add an element |
| `s.remove(item)` | — | Remove an element |
| `s.has(item)` | `Bool` | Check membership |
| `s.length()` | `Int` | Number of elements |
| `s.to_list()` | `List` | Convert to list |

### Set operations

| Function | Description |
|----------|-------------|
| `Collections.Set.union(a, b)` | Elements in either set |
| `Collections.Set.intersection(a, b)` | Elements in both sets |
| `Collections.Set.difference(a, b)` | Elements in a but not b |

### Example

```saffron
import "@collections" as Collections

var a = Collections.Set.from([1, 2, 3, 4])
var b = Collections.Set.from([3, 4, 5, 6])

var both = Collections.Set.intersection(a, b)
IO.println(both.to_list())  // [3, 4]
```

## Queue

A FIFO (first-in, first-out) queue.

```saffron
import "@collections" as Collections

var q = Collections.Queue.new()
q.enqueue("first")
q.enqueue("second")
IO.println(q.dequeue())  // "first"
IO.println(q.length())   // 1
IO.println(q.peek())     // "second"
IO.println(q.is_empty()) // false
```

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `q.enqueue(item)` | — | Add to back |
| `q.dequeue()` | element | Remove from front |
| `q.peek()` | element | View front without removing |
| `q.length()` | `Int` | Number of elements |
| `q.is_empty()` | `Bool` | True if empty |
| `q.to_list()` | `List` | Convert to list (front to back) |

## Stack

A LIFO (last-in, first-out) stack.

```saffron
import "@collections" as Collections

var s = Collections.Stack.new()
s.push("bottom")
s.push("top")
IO.println(s.pop())      // "top"
IO.println(s.peek())     // "bottom"
IO.println(s.is_empty()) // false
```

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `s.push(item)` | — | Push onto top |
| `s.pop()` | element | Remove from top |
| `s.peek()` | element | View top without removing |
| `s.length()` | `Int` | Number of elements |
| `s.is_empty()` | `Bool` | True if empty |
| `s.to_list()` | `List` | Convert to list (top to bottom) |

## Deque

A double-ended queue — efficient push/pop from both ends.

```saffron
import "@collections" as Collections

var d = Collections.Deque.new()
d.push_back(1)
d.push_front(0)
IO.println(d.pop_front())  // 0
IO.println(d.pop_back())   // 1
```

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `d.push_front(item)` | — | Add to front |
| `d.push_back(item)` | — | Add to back |
| `d.pop_front()` | element | Remove from front |
| `d.pop_back()` | element | Remove from back |
| `d.peek_front()` | element | View front |
| `d.peek_back()` | element | View back |
| `d.length()` | `Int` | Number of elements |
| `d.is_empty()` | `Bool` | True if empty |
