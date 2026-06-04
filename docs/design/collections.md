# Collections Modules

Additional collection types for the Saffron standard library. Each is a separate importable module, implemented in pure Saffron (no runtime/C support needed), backed by Lists with appropriate algorithms.

All collections implement the iterator protocol (`iter()` returning an object with `next?()` and `next()`) so they work with `for-in` loops and `@iter` utilities.

---

## @deque — Double-ended queue

Backed by two lists (front reversed, back forward). Amortized O(1) push/pop on both ends.

```saffron
import "@deque" as Deque

var dq = Deque.new()
dq.push_front(1)
dq.push_back(2)
dq.pop_front()    // 1
dq.pop_back()     // 2
dq.peek_front()   // first element without removing
dq.peek_back()    // last element without removing
dq.length()       // number of elements
dq.is_empty()     // true if empty
dq.to_list()      // snapshot as List (front-to-back order)
dq.iter()         // iterator, front-to-back
```

### Implementation

Two internal lists: `_front` (reversed) and `_back`. `push_front` appends to `_front`, `push_back` appends to `_back`. When one side is empty on pop, the other side is split and reversed to rebalance. This gives amortized O(1) for all operations.

### Complexity

| Operation | Time |
|-----------|------|
| push_front / push_back | O(1) |
| pop_front / pop_back | O(1) amortized |
| peek_front / peek_back | O(1) |
| length | O(1) (cached counter) |

---

## @heap — Binary min/max heap (priority queue)

Array-based binary heap. O(log n) insert and extract, O(1) peek.

```saffron
import "@heap" as Heap

var h = Heap.min()           // min-heap (smallest first)
h.push(5)
h.push(1)
h.push(3)
h.peek()                     // 1
h.pop()                      // 1
h.pop()                      // 3
h.length()                   // 1
h.is_empty()                 // false
h.to_list()                  // elements in heap order (not sorted)
h.iter()                     // pops in priority order (destructive)

var mh = Heap.max()          // max-heap (largest first)

// Custom comparator: returns true if a has higher priority than b
var custom = Heap.new(fun (a, b) => a.priority < b.priority)
```

### Implementation

Single list `_data` using standard array-based binary heap layout (parent at `i`, children at `2*i+1` and `2*i+2`). `push` appends then sifts up; `pop` swaps root with last, removes last, sifts down. Comparator stored as `_cmp` function.

`Heap.min()` uses `fun (a, b) => a < b`. `Heap.max()` uses `fun (a, b) => a > b`.

### Complexity

| Operation | Time |
|-----------|------|
| push | O(log n) |
| pop | O(log n) |
| peek | O(1) |
| length | O(1) |

---

## @sorted_map — Ordered map (keys maintained in sorted order)

Keys stored in a sorted list; binary search for lookup and insertion point.

```saffron
import "@sorted_map" as SortedMap

var sm = SortedMap.new()     // default: key < comparison
sm.set("banana", 2)
sm.set("apple", 1)
sm.set("cherry", 3)
sm.get("apple")              // 1
sm.has("banana")             // true
sm.remove("banana")
sm.keys()                    // ["apple", "cherry"] — sorted
sm.values()                  // [1, 3] — in key order
sm.entries()                 // [["apple", 1], ["cherry", 3]]
sm.first()                   // ["apple", 1]
sm.last()                    // ["cherry", 3]
sm.range("b", "d")          // [["cherry", 3]] — keys in [b, d)
sm.length()
sm.is_empty()
sm.iter()                    // iterates [key, value] pairs in order

// Custom key comparator
var sm2 = SortedMap.new(fun (a, b) => a < b)
```

### Implementation

Two parallel lists: `_keys` (sorted) and `_values`. Binary search (`_bisect`) finds insertion/lookup index. `set` inserts at the correct position or updates in-place. `range` uses two binary searches to find the slice bounds.

### Complexity

| Operation | Time |
|-----------|------|
| get / has | O(log n) |
| set / remove | O(n) (shift on insert/delete) |
| first / last | O(1) |
| range | O(log n + k) where k = result size |
| keys / values | O(n) copy |

Future optimization: B-tree backing for O(log n) insert/remove.

---

## @sorted_set — Ordered set (unique elements in sorted order)

Thin wrapper: sorted list with binary-search deduplication.

```saffron
import "@sorted_set" as SortedSet

var ss = SortedSet.new()
ss.add(5)
ss.add(1)
ss.add(3)
ss.add(1)                    // no-op, already present
ss.has(3)                    // true
ss.remove(3)
ss.to_list()                 // [1, 5] — sorted
ss.min()                     // 1
ss.max()                     // 5
ss.range(2, 6)               // [5] — elements in [2, 6)
ss.length()
ss.is_empty()
ss.iter()                    // iterates in sorted order

// Set operations (return new SortedSet)
var a = SortedSet.from([1, 3, 5])
var b = SortedSet.from([2, 3, 4])
SortedSet.union(a, b).to_list()      // [1, 2, 3, 4, 5]
SortedSet.intersect(a, b).to_list()  // [3]
SortedSet.diff(a, b).to_list()       // [1, 5]

// Custom comparator
var ss2 = SortedSet.new(fun (a, b) => a < b)
```

### Implementation

Single sorted list `_items`. Binary search for `add` (skip if present), `has`, `remove`. `min()`/`max()` read first/last element. Set operations use merge-style linear scan over two sorted lists for O(n) union/intersect/diff.

### Complexity

| Operation | Time |
|-----------|------|
| add / remove | O(n) (shift) |
| has | O(log n) |
| min / max | O(1) |
| range | O(log n + k) |
| union / intersect / diff | O(n + m) |

---

## @queue — FIFO queue

Lightweight single-purpose queue. Backed by a list with head pointer to avoid O(n) shift on dequeue.

```saffron
import "@queue" as Queue

var q = Queue.new()
q.enqueue("first")
q.enqueue("second")
q.dequeue()                  // "first"
q.peek()                     // "second"
q.length()                   // 1
q.is_empty()                 // false
q.to_list()                  // ["second"]
q.iter()                     // front-to-back
```

### Implementation

List `_data` with integer `_head` tracking the logical front. `enqueue` appends. `dequeue` reads `_data[_head]` and increments `_head`. When `_head > _data.length() / 2`, compact by slicing off consumed prefix. This avoids O(n) shifts while keeping memory bounded.

### Complexity

| Operation | Time |
|-----------|------|
| enqueue | O(1) |
| dequeue | O(1) amortized |
| peek | O(1) |
| length | O(1) |

---

## @stack — LIFO stack

Explicit stack semantics. Trivially backed by a list.

```saffron
import "@stack" as Stack

var s = Stack.new()
s.push(1)
s.push(2)
s.peek()                     // 2
s.pop()                      // 2
s.length()                   // 1
s.is_empty()                 // false
s.to_list()                  // [1] (bottom-to-top)
s.iter()                     // top-to-bottom
```

### Implementation

Single list `_data`. `push` appends, `pop`/`peek` operate on the last element. All operations are direct List method calls.

### Complexity

| Operation | Time |
|-----------|------|
| push / pop / peek | O(1) |
| length | O(1) |

---

## Common patterns

All collections follow these conventions (matching `@set`):

- **Constructor**: `Module.new()` or factory functions (`Heap.min()`, `Heap.max()`)
- **Convenience builder**: `Module.from(list)` to construct from an existing list
- **Chainable mutators**: mutating methods return `this` where it makes sense
- **Iterator protocol**: `iter()` returns an object with `next?(): Bool` and `next(): T`
- **Conversion**: `to_list()` produces a plain List snapshot
- **Inspection**: `length()`, `is_empty()`, `to_string()`
- **Pure Saffron**: no C runtime extensions required; all implementable today

## Open questions

1. **Generic types** — Should constructors accept a type parameter (`Queue<Number>.new()`) once generics on classes are fully stable, or stay `Any`-typed like `@set`?
2. **Immutable variants** — Worth providing frozen/persistent versions (returning new instances on mutation)?
3. **Comparator defaults** — For sorted collections, should the default comparator use `<` operator or require explicit provision when element types lack natural ordering?
