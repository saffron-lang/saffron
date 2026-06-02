#!/usr/bin/env bash
# build_prelude.sh — Assemble prelude.sf from infrastructure + generated elements
#
# Usage: ./tools/build_prelude.sh
#   (run from turmeric/ directory)
#
# Prerequisites: src/generated_prelude.sf must exist (run generate_types.sf first)

set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
GENERATED="$DIR/src/generated_prelude.sf"
OUTPUT="$DIR/src/prelude.sf"

if [[ ! -f "$GENERATED" ]]; then
    echo "Error: $GENERATED not found. Run generate_types.sf first." >&2
    exit 1
fi

cat > "$OUTPUT" << 'INFRASTRUCTURE'
// Turmeric Prelude — auto-imported into any app that depends on turmeric
// Provides HTML element builder functions that create real DOM nodes
//
// Element functions are auto-generated from the TypeScript DOM spec.
// To regenerate: saffron run tools/generate_types.sf
// Then run: tools/build_prelude.sh (concatenates infra + generated elements)

// --- DOM FFI (provided by wasm_base.ll) ---
@extern("i64 js_dom_create_element(void*)") fun _tc_create(tag: String): Float
@extern("void js_dom_set_text(i64, void*)") fun _tc_set_text(handle: Float, text: String)
@extern("void js_dom_append_child(i64, i64)") fun _tc_append(parent: Float, child: Float)
@extern("void js_dom_set_attr(i64, void*, void*)") fun _tc_set_attr(handle: Float, name: String, value: String)
@extern("void js_dom_add_event(i64, void*, i64)") fun _tc_add_event(handle: Float, event: String, handler: Any)

// --- Context stack: tracks current parent for nesting ---
var _tc_stack: List<Float> = []
var _tc_root: Float = 0

fun _tc_push(handle: Float) {
    _tc_stack.push(handle)
}

fun _tc_pop(): Float {
    if (_tc_stack.length() == 0) {
        throw "turmeric: element stack underflow — more pops than pushes (check nesting)"
    }
    return _tc_stack.pop()
}

fun _tc_current(): Float {
    if (_tc_stack.length() == 0) { return _tc_root }
    return _tc_stack[_tc_stack.length() - 1]
}

// --- Text node ---
fun text(content: String) {
    var parent: Float = _tc_current()
    _tc_set_text(parent, content)
}

// --- Attribute application: maps keys to DOM operations ---
fun _tc_apply_attrs(el: Float, attrs: Map<String, Any>) {
    var keys = attrs.keys()
    var i = 0
    while (i < keys.length()) {
        var key = keys[i]
        var value = attrs.get(key)
        if (key == "cls" or key == "class") {
            _tc_set_attr(el, "class", value)
        } else if (key == "id") {
            _tc_set_attr(el, "id", value)
        } else if (key.starts_with("on_")) {
            var event_name = key.slice(3, key.length())
            _tc_add_event(el, event_name, value)
        } else {
            _tc_set_attr(el, key, value)
        }
        i = i + 1
    }
}

// --- Common attribute helper: creates element, sets typed common attrs, applies overflow, nests block ---
fun _tc_el_with_common(tag: String, cls: String, id: String, style: String,
                       attrs: Map<String, Any>, block: Any): Float {
    var el: Float = _tc_create(tag)
    if (cls.length() > 0) { _tc_set_attr(el, "class", cls) }
    if (id.length() > 0) { _tc_set_attr(el, "id", id) }
    if (style.length() > 0) { _tc_set_attr(el, "style", style) }
    _tc_apply_attrs(el, attrs)
    var parent: Float = _tc_current()
    _tc_append(parent, el)
    if (block != nil) {
        _tc_push(el)
        block()
        _tc_pop()
    }
    return el
}

// =============================================================================
// Element Builder Functions (auto-generated from DOM spec via generate_types.sf)
// DO NOT EDIT below this line manually — regenerate with tools/build_prelude.sh
// =============================================================================

INFRASTRUCTURE

# Append generated element functions
cat "$GENERATED" >> "$OUTPUT"

# Append mount function
cat >> "$OUTPUT" << 'MOUNT'

// =============================================================================
// Mount: set the root element and call the app function
// =============================================================================

fun mount(selector: String, app: Any) {
    @extern("i64 js_dom_query_selector(void*)") fun _tc_query(sel: String): Float
    _tc_root = _tc_query(selector)
    app()
}
MOUNT

echo "Built $OUTPUT ($(wc -l < "$OUTPUT") lines)"
