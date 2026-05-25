#!/bin/bash
# Native Compiler Test Suite
# Runs after the native saffronc binary is functional
# Tests every major language feature through compilation

set -e
SAFFRONC="${1:-/tmp/saffronc}"
PASS=0
FAIL=0

test_compile_run() {
    local name="$1"
    local source="$2"
    local expected="$3"

    echo "$source" > /tmp/test_$name.sf
    if $SAFFRONC /tmp/test_$name.sf /tmp/test_$name.ll 2>/dev/null && \
       clang -O2 -o /tmp/test_$name /tmp/test_$name.ll 2>/dev/null; then
        local actual=$(timeout 2 /tmp/test_$name 2>/dev/null)
        if [ "$actual" = "$expected" ]; then
            echo "  PASS: $name"
            PASS=$((PASS + 1))
        else
            echo "  FAIL: $name (expected '$expected', got '$actual')"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "  FAIL: $name (compilation failed)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Saffron Native Compiler Test Suite ==="
echo "Using: $SAFFRONC"
echo ""

# Basic
test_compile_run "hello" \
    'fun main(): Int { IO.println("Hello!"); return 0 }' \
    "Hello!"

test_compile_run "arithmetic" \
    'fun main(): Int { IO.println(2 + 3 * 4); return 0 }' \
    "14"

test_compile_run "string_concat" \
    'fun main(): Int { IO.println("a" + "b" + "c"); return 0 }' \
    "abc"

# Control flow
test_compile_run "if_else" \
    'fun main(): Int { if (5 > 3) { IO.println("yes") } else { IO.println("no") }; return 0 }' \
    "yes"

test_compile_run "while_loop" \
    'fun main(): Int { var i: Int = 0; while (i < 5) { i = i + 1 }; IO.println(i); return 0 }' \
    "5"

# Functions
test_compile_run "function_call" \
    'fun double(x: Int): Int { return x * 2 }
fun main(): Int { IO.println(double(21)); return 0 }' \
    "42"

# Classes
test_compile_run "class_basic" \
    'class Counter { var n: Int; fun init() { this.n = 0 }; fun inc() { this.n = this.n + 1 }; fun get(): Int { return this.n } }
fun main(): Int { var c = Counter(); c.init(); c.inc(); c.inc(); IO.println(c.get()); return 0 }' \
    "2"

# Lists
test_compile_run "list_ops" \
    'fun main(): Int { var l = [10, 20, 30]; l.push(40); IO.println(l.length()); IO.println(l[3]); return 0 }' \
    "4
40"

# Strings
test_compile_run "string_methods" \
    'fun main(): Int { var s: String = "hello"; IO.println(s.length()); IO.println(s.char_at(0)); return 0 }' \
    "5
h"

echo ""
echo "Results: $PASS passed, $FAIL failed"
exit $FAIL
