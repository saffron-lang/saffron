#!/usr/bin/env bash
# saffron — unified driver for the Saffron language
#
# Usage:
#   saffron run file.sf          Run a .sf file (compile + execute)
#   saffron build file.sf -o out Compile to native binary
#   saffron emit-ir file.sf      Output LLVM IR (for debugging)
#   saffron file.sf              Shorthand for: saffron run file.sf
#
# This wraps saffronc + clang into a single user-facing command.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRONC="${SAFFRONC:-$SCRIPT_DIR/build/saffronc}"
RUNTIME="${SAFFRON_RUNTIME:-$SCRIPT_DIR/build/stage3/runtime.ll}"
RUNTIME_BASE="${SAFFRON_RUNTIME_BASE:-$SCRIPT_DIR/src/runtime/base.ll}"

usage() {
    echo "saffron — The Saffron Programming Language"
    echo ""
    echo "Usage:"
    echo "  saffron run <file.sf>              Run a Saffron program"
    echo "  saffron build <file.sf> -o <out>   Compile to native binary"
    echo "  saffron build <file.sf>            Compile to ./a.out"
    echo "  saffron emit-ir <file.sf>          Print LLVM IR to stdout"
    echo "  saffron <file.sf>                  Shorthand for: saffron run <file.sf>"
    echo "  saffron version                    Show version"
    echo ""
    echo "Options:"
    echo "  -o <path>    Output binary path (for build)"
    echo "  --opt, -O    Optimization level (default: -O2)"
    echo "  --verbose    Show compilation steps"
    exit 0
}

error() {
    echo "saffron: $1" >&2
    exit 1
}

# --- Parse arguments ---

COMMAND=""
INPUT=""
OUTPUT=""
OPT="-O2"
VERBOSE=false

if [[ $# -eq 0 ]]; then
    usage
fi

case "$1" in
    run|build|emit-ir|version|--help|-h)
        COMMAND="$1"
        shift
        ;;
    *.sf)
        COMMAND="run"
        ;;
    *)
        error "unknown command '$1'. Run 'saffron --help' for usage."
        ;;
esac

if [[ "$COMMAND" == "--help" || "$COMMAND" == "-h" ]]; then
    usage
fi

if [[ "$COMMAND" == "version" ]]; then
    echo "saffron 0.1.0 (native)"
    exit 0
fi

# Parse remaining args
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o)
            shift
            OUTPUT="$1"
            ;;
        --opt|-O)
            shift
            OPT="-O$1"
            ;;
        --verbose)
            VERBOSE=true
            ;;
        *.sf)
            INPUT="$1"
            ;;
        *)
            # Pass through as program arguments for 'run'
            break
            ;;
    esac
    shift
done

if [[ -z "$INPUT" && "$COMMAND" != "version" ]]; then
    error "no input file specified"
fi

if [[ ! -f "$INPUT" ]]; then
    error "file not found: $INPUT"
fi

# --- Check prerequisites ---

if [[ ! -x "$SAFFRONC" ]]; then
    error "saffronc not found at $SAFFRONC. Build with: ./bootstrap.sh"
fi

if ! command -v clang &>/dev/null; then
    error "clang not found. Install LLVM/Clang to compile Saffron programs."
fi

# --- Commands ---

compile_to_ir() {
    local input="$1"
    local output="$2"
    [[ "$VERBOSE" == true ]] && echo "  compile: $input -> $output"
    "$SAFFRONC" "$input" "$output"
}

link_binary() {
    local ir="$1"
    local output="$2"
    [[ "$VERBOSE" == true ]] && echo "  link: $ir + runtime -> $output"
    clang "$OPT" -o "$output" "$ir" "$RUNTIME" "$RUNTIME_BASE" 2>&1
}

case "$COMMAND" in
    emit-ir)
        TMPIR=$(mktemp /tmp/saffron_XXXXXX.ll)
        compile_to_ir "$INPUT" "$TMPIR"
        cat "$TMPIR"
        rm -f "$TMPIR"
        ;;
    build)
        if [[ -z "$OUTPUT" ]]; then
            OUTPUT="a.out"
        fi
        TMPIR=$(mktemp /tmp/saffron_XXXXXX.ll)
        trap "rm -f $TMPIR" EXIT
        compile_to_ir "$INPUT" "$TMPIR"
        link_binary "$TMPIR" "$OUTPUT"
        [[ "$VERBOSE" == true ]] && echo "  done: $OUTPUT"
        ;;
    run)
        TMPIR=$(mktemp /tmp/saffron_XXXXXX.ll)
        TMPBIN=$(mktemp /tmp/saffron_XXXXXX)
        trap "rm -f $TMPIR $TMPBIN" EXIT
        compile_to_ir "$INPUT" "$TMPIR"
        link_binary "$TMPIR" "$TMPBIN"
        chmod +x "$TMPBIN"
        "$TMPBIN" "$@"
        ;;
esac
