// wasm_run.mjs — minimal host for a Saffron wasm32/wasm64 module.
//
// This is the wasm half of the differential oracle (tools/differential.sh). It
// exists so that the SAME .sf program can be run on a second backend and its
// stdout compared byte-for-byte against the native binary's. Any difference is a
// codegen or runtime bug in one of the two, and the class of bug it finds is
// precisely the class that exit-code testing cannot see: BUGS #77 was
// `__bool_to_string` on wasm32 untagging an already-untagged value, so every
// `true` printed as "false" while branching on the same value stayed correct.
// Nothing in the existing suite could observe that. A wasm-vs-native stdout diff
// observes it on the first line of output.
//
// Usage:  node tools/oracle/wasm_run.mjs module.wasm
//
// Output goes to stdout, one line per IO.println, matching the native runtime's
// line discipline — that byte-identity is the whole point, so any formatting
// decision here must mirror src/runtime/wasm_base_32.ll's js_log_* contract
// rather than being chosen for readability.
//
// UNPROVIDED IMPORTS
//
// Saffron's wasm bases declare far more host functions than any single program
// uses (file I/O, sockets, process control, clocks — all meaningless in wasm),
// and wasm-ld is invoked with --import-undefined, so a module's import list is
// large and program-dependent. Enumerating it by hand would make this host a
// maintenance burden that drifts out of date; stubbing makes the host indifferent
// to which subset a program pulls in.
//
// But the stubs must be SIGNATURE-CORRECT, which is why this file parses the
// module's type and import sections instead of installing a blanket `() => 0`.
// A JS `0` returned where the module declares an `i64` result throws
// "Cannot convert 0 to a BigInt" at the call site — and the first version of this
// script did exactly that, which made 8 of 13 wasm mismatches in the first full
// differential run artifacts of the harness rather than findings about the
// compiler. A harness that manufactures its own failures is worse than no
// harness, because every real finding then has to be argued for.
//
// The remaining tradeoff is unavoidable: a program that genuinely depends on one
// of those hosts gets 0/0n and produces plausible-looking wrong output. That is
// why tools/differential.sh has a capability gate that skips such programs by
// import rather than trusting the shim to be honest about them.

import fs from 'fs';

const modulePath = process.argv[2];
if (!modulePath) {
  process.stderr.write('usage: node wasm_run.mjs <module.wasm>\n');
  process.exit(2);
}

const buf = fs.readFileSync(modulePath);

// --- Minimal wasm binary reader: enough to recover imported function signatures.
//
// Only sections 1 (Type) and 2 (Import) are decoded; everything else is skipped
// by its length prefix. This is a read of the format's fixed preamble, not a
// general decoder, so it stays short and has no opinion about the module's code.

function sigReader(bytes) {
  let p = 0;
  const u8 = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);

  const byte = () => u8[p++];
  const u32 = () => {
    // LEB128 unsigned.
    let result = 0, shift = 0, b;
    do {
      b = u8[p++];
      result |= (b & 0x7f) << shift;
      shift += 7;
    } while (b & 0x80);
    return result >>> 0;
  };
  const name = () => {
    const len = u32();
    const s = new TextDecoder().decode(u8.subarray(p, p + len));
    p += len;
    return s;
  };

  // Magic + version.
  p = 8;

  const funcTypes = [];   // index -> { params: [valtype], results: [valtype] }
  const importedFuncs = []; // in index order -> { module, field, type }

  while (p < u8.length) {
    const id = byte();
    const size = u32();
    const end = p + size;

    if (id === 1) {
      const n = u32();
      for (let i = 0; i < n; i++) {
        byte(); // 0x60 functype
        const np = u32();
        const params = [];
        for (let j = 0; j < np; j++) params.push(byte());
        const nr = u32();
        const results = [];
        for (let j = 0; j < nr; j++) results.push(byte());
        funcTypes.push({ params, results });
      }
    } else if (id === 2) {
      const n = u32();
      for (let i = 0; i < n; i++) {
        const mod = name();
        const field = name();
        const kind = byte();
        if (kind === 0x00) {
          const typeIdx = u32();
          importedFuncs.push({ module: mod, field, type: funcTypes[typeIdx] });
        } else if (kind === 0x01) {        // table
          byte(); u32(); /* limits */
          // flags bit 0 set means a max follows
          p--; const fl = u8[p++]; if (fl & 1) u32();
        } else if (kind === 0x02) {        // memory
          const fl = byte(); u32(); if (fl & 1) u32();
        } else if (kind === 0x03) {        // global
          byte(); byte();
        }
      }
    }

    p = end;
  }

  return importedFuncs;
}

// valtype 0x7e is i64. An i64 result must be returned as a BigInt; an i64
// PARAMETER arrives as a BigInt, which the stubs simply ignore.
const I64 = 0x7e;

let imported = [];
try {
  imported = sigReader(buf);
} catch (e) {
  // A parse failure must not be fatal: fall back to untyped stubs. It degrades
  // to the old behaviour for i64-returning imports rather than refusing to run.
  process.stderr.write(
    'wasm_run: warning: could not read import signatures (' +
      (e && e.message ? e.message : e) +
      '); i64-returning stubs may trap\n'
  );
}

// field name -> a stub with the right return type for THIS module.
const stubFor = new Map();
for (const imp of imported) {
  const results = (imp.type && imp.type.results) || [];
  let stub;
  if (results.length === 0) stub = () => {};
  else if (results[0] === I64) stub = () => 0n;
  else stub = () => 0;
  stubFor.set(imp.field, stub);
}

let mem;
const dec = new TextDecoder();

// Read a NUL-terminated C string out of the instance's linear memory. mem.buffer
// is re-read on every call rather than cached: memory.grow detaches the old
// ArrayBuffer, and a cached Uint8Array over it reads as all zeroes afterwards.
const cstr = (p) => {
  const u8 = new Uint8Array(mem.buffer);
  let i = Number(p);
  let e = i;
  while (u8[e] !== 0) e++;
  return dec.decode(u8.subarray(i, e));
};

// Buffer output and write once at the end. The differential compares whole
// streams, so a tail lost to an interleaved crash would read as a mismatch — a
// wrong finding, and the expensive kind to chase.
const out = [];
const emit = (s) => out.push(s);

// The four logging hosts are the ones whose behaviour actually matters; their
// formatting must match the native runtime's exactly, because a formatting
// difference here is indistinguishable from a codegen bug in the diff.
// `js_log_int` may receive a BigInt on wasm64, hence String() rather than
// template concatenation.
const env = {
  js_log_str: (p) => emit(cstr(p)),
  js_log_int: (n) => emit(String(n)),
  js_log_bool: (b) => emit(BigInt(b) !== 0n ? 'true' : 'false'),
  js_log_nil: () => emit('nil'),
};

// libm. wasm has no libc, so `@math`'s externs (round, pow, cos, log2, ...)
// become plain module imports and the host must supply them. These are NOT
// stubbed, they are implemented, because JS's Math is IEEE-754 double math on the
// same hardware as native libm — so `Math.pow(2, 10)` and C `pow(2, 10)` agree
// bit-for-bit, and the differential can therefore grade a math program instead
// of skipping it.
//
// Before this, stubbing them to 0 made test/stdlib_math.sf report five FAILing
// assertions on wasm ("expected: 1024, actual: 0") that were purely the shim's
// doing. That is the exact failure mode this file's header warns about: the
// harness manufacturing findings. Anything added here must be a faithful
// implementation, never a placeholder — a wrong implementation here is worse
// than an absent one, because it looks like a compiler bug.
//
// sqrt/sin/cos/tan/log/exp/atan2/fabs are the same function in both languages.
// `round` is NOT: C's round() breaks ties away from zero (round(-2.5) == -3)
// while JS's Math.round breaks ties toward +Infinity (Math.round(-2.5) == -2).
// Getting that wrong would show up as a wasm-only mismatch on negative halves,
// so it is written out rather than delegated.
const cRound = (x) => (x < 0 ? -Math.round(-x) : Math.round(x));

const libm = {
  round: cRound,
  sqrt: Math.sqrt,
  pow: Math.pow,
  sin: Math.sin,
  cos: Math.cos,
  tan: Math.tan,
  asin: Math.asin,
  acos: Math.acos,
  atan: Math.atan,
  atan2: Math.atan2,
  log: Math.log,
  log2: Math.log2,
  log10: Math.log10,
  exp: Math.exp,
  floor: Math.floor,
  ceil: Math.ceil,
  fabs: Math.abs,
  fmod: (a, b) => a % b,
  trunc: Math.trunc,
};

// Only install a libm function if the module actually declares it as an f64
// import. Guarding on the declared signature keeps this from shadowing a
// same-named Saffron export or an i64-typed import that happens to collide.
const importedByName = new Map(imported.map((i) => [i.field, i]));
for (const [name, fn] of Object.entries(libm)) {
  const imp = importedByName.get(name);
  if (!imp) continue;
  const t = imp.type || { params: [], results: [] };
  const allF64 =
    t.params.every((p) => p === 0x7c) && t.results.every((r) => r === 0x7c);
  if (allF64) env[name] = fn;
}

let status = 0;
try {
  const { instance } = await WebAssembly.instantiate(buf, {
    env: new Proxy(env, {
      get: (t, k) => {
        if (k in t) return t[k];
        const s = stubFor.get(k);
        if (s) return s;
        // Not in the import table at all: the Proxy is also consulted for
        // internal property reads during instantiation, so an untyped stub is
        // the right fallback and cannot be called by the module.
        return () => 0;
      },
      has: () => true,
    }),
  });
  mem = instance.exports.memory;
  const entry =
    instance.exports._start ||
    instance.exports.main ||
    instance.exports.__saffron_main;
  if (!entry) {
    process.stderr.write('wasm_run: module exports no entry point\n');
    process.exit(3);
  }
  entry();
} catch (e) {
  // Report on stderr and exit nonzero. The differential treats a nonzero wasm
  // exit as "wasm-side failure", distinct from a mismatch: a trap is a different
  // finding than a wrong answer and should not be filed as one.
  process.stderr.write(
    'wasm_run: ' + (e && e.message ? e.message : String(e)) + '\n'
  );
  status = 4;
}

if (out.length) process.stdout.write(out.join('\n') + '\n');
process.exit(status);
