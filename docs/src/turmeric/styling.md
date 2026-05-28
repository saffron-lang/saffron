# Styling

Turmeric provides multiple approaches to CSS — from inline styles to scoped component styles to utility-first helpers.

## Inline Styles

The simplest approach — pass a style string directly:

```saffron
div([style("display: flex; gap: 1rem; padding: 1rem")]) {
    span([style("color: red; font-weight: bold")]) { text("Alert!") }
}
```

## Style Maps

For dynamic or conditional styles, use a style map:

```saffron
fun styles(pairs: Map<String, String>): Attr {
    var parts: List<String> = []
    for (key in pairs.keys()) {
        parts.push(key + ": " + pairs.get(key))
    }
    return style(parts.join("; "))
}

// Usage:
div([styles({
    "display": "flex",
    "gap": "1rem",
    "background": if (dark.get()) "black" else "white"
})]) {
    text("Dynamic styles!")
}
```

## Class-Based Styling

Use `class_()` with external CSS (linked stylesheet or `<style>` tag):

```saffron
div([class_("card shadow-md rounded-lg p-4")]) {
    h2([class_("text-xl font-bold")]) { text(title) }
    p([class_("text-gray-600 mt-2")]) { text(body) }
}
```

### Conditional Classes

```saffron
fun cx(classes: List<List<String>>): Attr {
    var result: List<String> = []
    for (pair in classes) {
        if (pair[1] == "true") {
            result.push(pair[0])
        }
    }
    return class_(result.join(" "))
}

// Usage:
button([cx([
    ["btn", "true"],
    ["btn-primary", is_primary.to_string()],
    ["btn-disabled", is_loading.get().to_string()],
])]) {
    text("Submit")
}
```

## Scoped Styles

Turmeric can generate unique class names per component to avoid collisions:

```saffron
fun Card(title: String): Node {
    var s = scoped_styles({
        "container": "padding: 1rem; border: 1px solid #ddd; border-radius: 8px;",
        "title": "font-size: 1.25rem; font-weight: bold; margin: 0;",
        "body": "color: #666; margin-top: 0.5rem;"
    })

    return div([class_(s.get("container"))]) {
        h2([class_(s.get("title"))]) { text(title) }
        p([class_(s.get("body"))]) { text("Card content") }
    }
}
```

`scoped_styles` generates unique class names (e.g., `card_container_x7k2`) and injects the corresponding CSS rules into a `<style>` tag.

## CSS Variables (Custom Properties)

For theming, use CSS variables that signals can update:

```saffron
var theme = signal("light")

fun ThemeProvider(block: () => Nil): Node {
    var vars = computed(fun () => {
        if (theme.get() == "dark") {
            return {
                "--bg": "#1a1a1a",
                "--fg": "#ffffff",
                "--accent": "#4fc3f7"
            }
        }
        return {
            "--bg": "#ffffff",
            "--fg": "#1a1a1a",
            "--accent": "#1976d2"
        }
    })

    return div([styles(vars.get())]) {
        block()
    }
}
```

Components reference variables via regular CSS:

```saffron
div([style("background: var(--bg); color: var(--fg)")]) {
    text("Themed content")
}
```

## Animation

### Transitions

```saffron
var expanded = signal(false)

div([styles({
    "max-height": if (expanded.get()) "500px" else "0",
    "overflow": "hidden",
    "transition": "max-height 0.3s ease"
})]) {
    p { text("Expandable content") }
}
```

### Keyframe Animations

Define animations in CSS and toggle with classes:

```saffron
var visible = signal(false)

div([cx([
    ["fade-in", visible.get().to_string()],
    ["fade-out", (!visible.get()).to_string()]
])]) {
    text("Animated!")
}
```

## Media Queries

Use a signal that tracks viewport state:

```saffron
var is_mobile = media_query("(max-width: 768px)")

fun Layout(block: () => Nil): Node {
    if (is_mobile.get()) {
        return div([class_("mobile-layout")]) { block() }
    }
    return div([class_("desktop-layout")]) { block() }
}
```

`media_query()` returns a `Signal<Bool>` that updates when the match changes.

## Design Tokens

For design system consistency, define tokens as constants:

```saffron
// tokens.sf
var SPACING_SM: String = "0.5rem"
var SPACING_MD: String = "1rem"
var SPACING_LG: String = "2rem"

var COLOR_PRIMARY: String = "#1976d2"
var COLOR_ERROR: String = "#d32f2f"
var COLOR_SUCCESS: String = "#388e3c"

var RADIUS_SM: String = "4px"
var RADIUS_MD: String = "8px"
var RADIUS_LG: String = "16px"

var FONT_SM: String = "0.875rem"
var FONT_MD: String = "1rem"
var FONT_LG: String = "1.25rem"
```

Use them in styles:

```saffron
div([styles({
    "padding": SPACING_MD,
    "border-radius": RADIUS_MD,
    "font-size": FONT_MD,
    "color": COLOR_PRIMARY
})]) {
    text("Design system")
}
```

## Recommended Approach

For most apps:

1. **Global styles** — CSS variables for theming, reset, typography
2. **Utility classes** — Tailwind-style or custom utilities for layout/spacing
3. **Scoped styles** — for component-specific styling that shouldn't leak
4. **Inline styles** — for truly dynamic values (animations, signal-driven)

Turmeric doesn't force a single approach — use what fits your project.
