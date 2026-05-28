# Component Lifecycle

Turmeric components have a simple lifecycle: **create → mount → (update) → unmount**.

## The Component Class

Every Turmeric component owns a DOM subtree and manages its own cleanup:

```saffron
class Component {
    var handle: Float           // DOM element handle
    var parent_handle: Float    // parent DOM element
    var children: List<Component>
    var cleanup_fns: List<Any>  // called on unmount
    var mounted: Bool
}
```

## Lifecycle Phases

### Create

Allocate the DOM element and set up initial state. Effects are registered but haven't fired yet.

```saffron
var card = Component(create_element("div"), parent_handle)
set_attr(card.handle, "class", "card")
```

### Mount

Append to the DOM and activate effects:

```saffron
card.mount()  // calls dom_append(parent, card.handle)
```

### Update (reactive)

Signals trigger effects that update the DOM surgically. No explicit "update" call needed — effects re-run automatically:

```saffron
managed_effect(card, fun () => {
    set_text(title_el, user.get().name)
})
```

### Unmount

Removes from DOM, unsubscribes all effects, recursively unmounts children:

```saffron
card.unmount()
// 1. Unmounts all card.children (depth-first)
// 2. Runs all card.cleanup_fns (unsubscribes effects)
// 3. Removes card.handle from DOM
// 4. Resets state
```

## Managed Effects

A `managed_effect` binds an effect's lifetime to a component. When the component unmounts, the effect is automatically unsubscribed:

```saffron
fun UserCard(parent: Float, user: Signal): Component {
    var card = Component(create_element("div"), parent)
    var name_el = create_element("h2")
    append_child(card.handle, name_el)

    // This effect is cleaned up when card unmounts
    managed_effect(card, fun () => {
        set_text(name_el, user.get().name)
    })

    card.mount()
    return card
}
```

Without managed effects, unmounting a component would leave orphan subscriptions that fire into removed DOM nodes.

## Conditional Rendering

`conditional()` creates a slot that mounts/unmounts a component based on a boolean signal:

```saffron
var logged_in = signal(true)

conditional(root, logged_in, fun () => {
    var welcome = Component(create_element("div"), root)
    var msg = create_element("p")
    set_text(msg, "Welcome back!")
    append_child(welcome.handle, msg)
    return welcome
})

// Later:
logged_in.set(false)  // component unmounts, DOM removed, effects cleaned up
logged_in.set(true)   // fresh component created and mounted
```

## Match Slots (multi-branch)

For switching between multiple views based on state:

```saffron
var page = signal("home")

match_slot(root, page, {
    "home": fun () => build_home_page(root),
    "about": fun () => build_about_page(root),
    "settings": fun () => build_settings_page(root)
})

// Navigation:
page.set("about")  // home unmounts, about mounts
```

## Keyed List Reconciliation

For dynamic lists, `keyed_list` efficiently adds, removes, and reorders items:

```saffron
var todos = signal([
    {id: "1", text: "Buy milk"},
    {id: "2", text: "Write code"},
])

keyed_list(list_el, todos,
    fun (item) => item.id,           // key function
    fun (item) => {                   // builder
        var li = Component(create_element("li"), list_el)
        set_text(li.handle, item.text)
        li.mount()
        return li
    }
)

// Add item — only the new item gets a DOM node
todos.set([...todos.get(), {id: "3", text: "Ship it"}])

// Remove item — only that item's component unmounts
todos.set(todos.get().filter(fun (t) => t.id != "2"))

// Reorder — DOM nodes are moved, not recreated
todos.set(todos.get().reverse())
```

### How Reconciliation Works

1. Compare old keys vs new keys
2. **Removed** items: call `component.unmount()` (DOM removed, effects cleaned up)
3. **Added** items: call `builder_fn(item)` (new DOM created, effects started)
4. **Moved** items: reuse existing component, reorder in DOM via `dom_append`
5. **Unchanged** items: no-op (component + effects stay alive)

This is O(n) with a hash map for key lookups — same algorithm as React/Solid.

## Cleanup Order

When a component tree unmounts:

```
Root.unmount()
  ├── Child1.unmount()
  │     ├── Grandchild1.unmount()
  │     │     └── cleanup_fns run (unsubscribe effects)
  │     │     └── DOM removed
  │     └── cleanup_fns run
  │     └── DOM removed
  ├── Child2.unmount()
  │     └── ...
  └── Root cleanup_fns run
  └── Root DOM removed
```

Children unmount before parents (depth-first). This ensures child effects are cleaned up before the parent's DOM node disappears.

## Summary

| Phase | What happens |
|-------|-------------|
| Create | Allocate DOM handle, configure attrs |
| Mount | Append to DOM, register effects |
| Update | Signal changes → effects re-run → surgical DOM updates |
| Unmount | Depth-first child unmount → cleanup effects → remove from DOM |
