// Tests for the identifier scanner and the two pure helpers built on it.
//
// These are the parts of the server whose bugs are silent and destructive: the
// scanner decides what rename rewrites, so a false positive inside a comment or
// a string edits text the user did not mean to change, and a false negative
// inside `${...}` leaves a reference behind and breaks the program. Everything
// else in server.ts needs a live LSP client to exercise; these do not, so they
// are worth testing directly.
//
// Run: node --test editors/shared/test/
import { test } from "node:test";
import assert from "node:assert/strict";
import { scanIdentifiers, occurrencesOf, splitParams, callContextAt } from "../out/scan.js";

const names = (src) => scanIdentifiers(src).map((o) => o.name);

test("plain identifiers are found with correct offsets", () => {
  const occ = scanIdentifiers("var count = 1");
  assert.deepEqual(occ.map((o) => o.name), ["var", "count"]);
  assert.equal(occ[1].offset, 4);
  assert.equal(occ[1].length, 5);
});

test("line comments are skipped in all three forms", () => {
  assert.deepEqual(names("// count\nvar x = 1"), ["var", "x"]);
  assert.deepEqual(names("/// count\nvar x = 1"), ["var", "x"]);
  assert.deepEqual(names("//! count\nvar x = 1"), ["var", "x"]);
});

test("block comments nest, so an inner close does not end the outer", () => {
  // The regex version closes at the FIRST */ and would then report `leaked`.
  assert.deepEqual(names("/* a /* b */ leaked */ var x"), ["var", "x"]);
});

test("string bodies are skipped but interpolations are scanned", () => {
  assert.deepEqual(names('var s = "plain text"'), ["var", "s"]);
  assert.deepEqual(names('var s = "n is ${count}"'), ["var", "s", "count"]);
});

test("an interpolated occurrence is reported at its true offset", () => {
  const src = 'var s = "n is ${count}"';
  const occ = occurrencesOf(src, "count");
  assert.equal(occ.length, 1);
  assert.equal(src.slice(occ[0].offset, occ[0].offset + occ[0].length), "count");
});

test("escaped quotes do not end a string early", () => {
  // Without the backslash skip, the string ends at \" and `leaked` looks like code.
  assert.deepEqual(names('var s = "a \\" leaked" '), ["var", "s"]);
});

test("nested braces inside an interpolation end at the right place", () => {
  assert.deepEqual(names('var s = "${m.get("k")} tail" + after'), ["var", "s", "m", "get", "after"]);
});

test("qualifier is captured for a dotted access", () => {
  const occ = scanIdentifiers("Iter.map(xs)");
  const map = occ.find((o) => o.name === "map");
  assert.equal(map.qualifier, "Iter");
  assert.equal(occ.find((o) => o.name === "Iter").qualifier, null);
});

test("qualifier survives a line break in a method chain", () => {
  const occ = scanIdentifiers("value\n  .trim()");
  assert.equal(occ.find((o) => o.name === "trim").qualifier, "value");
});

test("occurrencesOf matches whole identifiers only", () => {
  // A substring regex would match `count` inside `counter` and `total_count`.
  assert.equal(occurrencesOf("count counter total_count count", "count").length, 2);
});

test("splitParams ignores commas nested in generics", () => {
  // The bug this exists to prevent: a naive comma split reports 3 params here.
  const parts = splitParams("(m: Map<String,Int>, n: Int)");
  assert.equal(parts.length, 2);
  assert.equal(parts[0].text.trim(), "m: Map<String,Int>");
  assert.equal(parts[1].text.trim(), "n: Int");
});

test("splitParams handles an empty list and a return type", () => {
  assert.deepEqual(splitParams("(): Float"), []);
  assert.equal(splitParams("(a: Int): Int").length, 1);
});

test("splitParams param offsets index into the label", () => {
  const label = "(a: Int, b: Int)";
  const [first, second] = splitParams(label);
  assert.equal(label.slice(first.start, first.end), "a: Int");
  assert.equal(label.slice(second.start, second.end).trim(), "b: Int");
});

test("callContextAt finds the callee and argument index", () => {
  const src = "add(1, 2)";
  assert.deepEqual(callContextAt(src, src.indexOf("1")), { name: "add", argIndex: 0 });
  assert.deepEqual(callContextAt(src, src.indexOf("2")), { name: "add", argIndex: 1 });
});

test("callContextAt reports the outer call from a nested one", () => {
  // Cursor after `g(1, 2), ` — the enclosing call is f, second argument.
  const src = "f(g(1, 2), )";
  const ctx = callContextAt(src, src.lastIndexOf(")"));
  assert.equal(ctx.name, "f");
  assert.equal(ctx.argIndex, 1);
});

test("callContextAt returns null outside any call", () => {
  assert.equal(callContextAt("var x = 1", 9), null);
});
