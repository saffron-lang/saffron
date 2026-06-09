#!/usr/bin/env bash
# build_prelude.sh — Assemble prelude.sf from split source files + generated elements
#
# Usage: ./tools/build_prelude.sh
#   (run from turmeric/ directory)
#
# Structure:
#   src/prelude/01_ffi.sf         — extern declarations
#   src/prelude/02_event.sf       — Event class, _tc_event, event_target_value
#   src/prelude/03_callbacks.sf   — _tc_callbacks, _tc_register_callback, __dispatch_event
#   src/prelude/04_stack.sf       — _tc_stack, _tc_push, _tc_pop, _tc_current, text()
#   src/prelude/05_reactive.sf    — reactive(), reactive_class(), reactive_attr(), reactive_show()
#   src/prelude/06_render.sf      — render_into()
#   src/prelude/07_helpers.sf     — _tc_apply_attrs, _tc_el_with_common
#   src/generated_prelude.sf      — AUTO-GENERATED element builders (from generate_types.sf)
#   src/prelude/99_mount.sf       — mount()
#
# Output: src/prelude.sf (assembled, do not edit directly)
#
# Prerequisites: src/generated_prelude.sf must exist (run generate_types.sf first)

set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
PRELUDE_DIR="$DIR/src/prelude"
GENERATED="$DIR/src/generated_prelude.sf"
OUTPUT="$DIR/src/prelude.sf"

if [[ ! -f "$GENERATED" ]]; then
    echo "Error: $GENERATED not found. Run generate_types.sf first." >&2
    exit 1
fi

if [[ ! -d "$PRELUDE_DIR" ]]; then
    echo "Error: $PRELUDE_DIR directory not found." >&2
    exit 1
fi

# Assemble: numbered prelude parts (01-98), then generated elements, then 99_mount
{
    # Cat all numbered parts except 99_*
    for f in "$PRELUDE_DIR"/[0-9][0-9]_*.sf; do
        case "$(basename "$f")" in
            99_*) continue ;;
        esac
        cat "$f"
    done

    # Separator + generated element builders
    printf '\n// =============================================================================\n'
    printf '// Element Builder Functions (auto-generated from DOM spec via generate_types.sf)\n'
    printf '// DO NOT EDIT below this line manually — regenerate with tools/build_prelude.sh\n'
    printf '// =============================================================================\n\n'
    cat "$GENERATED"

    # Mount (and any other 99_* files)
    for f in "$PRELUDE_DIR"/99_*.sf; do
        [ -f "$f" ] && cat "$f"
    done
} > "$OUTPUT"

echo "Built $OUTPUT ($(wc -l < "$OUTPUT") lines)"
