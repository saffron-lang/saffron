// Saffron Playground loader.
//
// This is a *consumer* of the Turmeric runtime, not a fork of it. The shared DOM
// glue, event dispatch, and NaN-box marshalling all come from turmeric.js via
// mount(); this file supplies only the three things the playground needs beyond
// a normal Turmeric app, passed through mount()'s options:
//
//   1. `js_run_wasm` — instantiates the *user's* compiled module as a second,
//      completely separate WebAssembly instance and captures everything it prints.
//      The UI module and the user module never share memory: each gets its own
//      WebAssembly.Memory, handle table and import object; the only channel
//      between them is the captured output string.
//   2. Playground FFI — the compile-service fetch, hash-source share links, and
//      the float-returning math imports (`fmod`, `js_time_now`) that a user
//      program may need.
//   3. A custom wasm loader — the UI module is served base64 from /api/app_wasm,
//      because a Saffron `String` cannot hold a raw wasm binary (its magic number
//      is `\0asm`, and the first byte terminates the string; BUGS.md #66).
//
// Previously this file was a hand-maintained copy of the whole runtime, which is
// how the event-dispatch fix and the NaN-box handle logic drifted out of it. It
// now shares turmeric.js, so those cannot drift again.
import { mount } from './turmeric.js';

const MAX_OUTPUT_CHARS = 200000;
// Cap on scheduler pump iterations, so a program that never completes its tasks
// terminates instead of spinning forever.
const MAX_PUMPS = 20000;

function base64ToBytes(b64) {
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
}

function readCStringFrom(exports, ptr) {
    const p = Number(ptr);
    if (p === 0) return "";
    const b = new Uint8Array(exports.memory.buffer);
    let e = p;
    while (e < b.length && b[e] !== 0) e++;
    return new TextDecoder().decode(b.slice(p, e));
}

// Instantiate the user's compiled module in complete isolation and capture its
// output. Nothing here touches the UI module's memory or the page.
async function runUserModule(b64, report) {
    let out = "";
    let truncated = false;
    const emit = (s) => {
        if (truncated) return;
        if (out.length + s.length > MAX_OUTPUT_CHARS) {
            out += "\n… output truncated at " + MAX_OUTPUT_CHARS + " characters.";
            truncated = true;
            return;
        }
        out += s;
    };

    let userInstance;
    const userRead = (ptr) => readCStringFrom(userInstance.exports, ptr);

    // Imports for the *user's* module. Deliberately minimal: console logging,
    // memcpy, math. No DOM, no fetch, no timers-with-side-effects.
    const userEnv = {
        js_log_str: (ptr) => emit(userRead(ptr) + "\n"),
        js_log_int: (n) => emit(String(Number(n)) + "\n"),
        js_log_float: (f) => emit(String(Number(f)) + "\n"),
        js_log_bool: (b) => emit((Number(b) ? "true" : "false") + "\n"),
        js_log_nil: () => emit("nil\n"),
        js_print_str: (ptr) => emit(userRead(ptr)),
        memcpy: (dst, src, len) => {
            new Uint8Array(userInstance.exports.memory.buffer).copyWithin(Number(dst), Number(src), Number(src) + Number(len));
            return dst;
        },
        memset: (dst, val, len) => {
            new Uint8Array(userInstance.exports.memory.buffer).fill(Number(val), Number(dst), Number(dst) + Number(len));
            return dst;
        },
        // Math imports that must return a real f64, not a BigInt.
        fmod: (a, b) => Number(a) % Number(b),
        pow: (a, b) => Math.pow(Number(a), Number(b)),
        sqrt: (a) => Math.sqrt(Number(a)),
        floor: (a) => Math.floor(Number(a)),
        ceil: (a) => Math.ceil(Number(a)),
        fabs: (a) => Math.abs(Number(a)),
        js_time_now: () => Date.now() / 1000,
        __string_intern: (ptr) => ptr,
        __builtin_trap: () => { throw new Error("trap"); },
    };

    try {
        const mod = await WebAssembly.compile(base64ToBytes(b64));
        // Supply a typed zero for any unanticipated import, rather than a blanket
        // BigInt (which would throw for an f64-returning import).
        for (const imp of WebAssembly.Module.imports(mod)) {
            if (imp.module !== "env") continue;
            if (userEnv[imp.name] !== undefined) continue;
            userEnv[imp.name] = () => 0;
        }

        userInstance = await WebAssembly.instantiate(mod, { env: userEnv });
        userInstance.exports._start();

        // Drive the cooperative scheduler: __sched_pump runs ready tasks and
        // returns nonzero while work remains. Without this any program using
        // Task.spawn / Async.sleep prints nothing after its first suspension.
        const pump = userInstance.exports.__sched_pump;
        if (typeof pump === "function") {
            let n = 0;
            for (; n < MAX_PUMPS; n++) {
                let more;
                try { more = pump(); }
                catch (err) { emit("\n[scheduler error: " + (err.message || String(err)) + "]\n"); break; }
                if (!Number(more)) break;
            }
            if (n >= MAX_PUMPS) {
                emit("\n[stopped: program still had pending tasks after " + MAX_PUMPS + " scheduler steps]\n");
            }
        }
        report(out);
    } catch (err) {
        const message = err && err.message ? err.message : String(err);
        report("!!" + (out ? out + "\n" : "") + message); // "!!" => render as error
    }
}

// Load the UI module from /api/app_wasm (base64), not /app.wasm (raw bytes): a
// Saffron String cannot serve the raw file (BUGS.md #66). Fall back to the raw
// path so a plain static server still works for frontend-only development.
async function loadUiModule() {
    try {
        const resp = await fetch('./api/app_wasm');
        if (resp.ok) {
            const text = (await resp.text()).trim();
            if (text.length > 0 && text[0] !== '{') return base64ToBytes(text);
        }
    } catch (_) { /* fall through */ }
    return await (await fetch('./app.wasm')).arrayBuffer();
}

// Playground-specific env imports, layered over the runtime's built-ins. `ctx`
// exposes the live instance helpers (readCString / writeCString / findExport).
//
// The fetch/run completion callbacks re-enter the wasm through an exported
// `(id: Float, response: String)` function (api.sf's __on_fetch_complete /
// __on_run_complete). Both args need the same NaN-box encoding the runtime uses
// for event dispatch:
//   - id  -> encodeId (TAG_INT|payload): __on_*_complete's `id >= 0` guard and
//            its list index both go through __val_untag_int, and a raw machine
//            integer decodes to 0, so every completion resolved callback 0.
//   - str -> a raw pointer widened to i64; api.sf reads the String from it and
//            codegen expects the pointer form for this hand-rolled FFI.
// This is exactly why the playground shares the runtime instead of forking it:
// getting this encoding right is the runtime's job, exposed here via ctx.encodeId.
function playgroundEnv(ctx) {
    const { readCString, writeCString, findExport, encodeId } = ctx;
    const complete = (exportName, id, text) => {
        const fn = findExport(exportName);
        if (fn) fn(encodeId(id), BigInt(writeCString(text)));
    };
    return {
        fmod: (a, b) => Number(a) % Number(b),
        js_time_now: () => Date.now() / 1000,

        js_fetch_post: (urlPtr, bodyPtr, callbackId) => {
            fetch(readCString(urlPtr), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: readCString(bodyPtr),
            }).then(r => r.text())
              .then(txt => complete('__on_fetch_complete', callbackId, txt))
              .catch(err => complete('__on_fetch_complete', callbackId, JSON.stringify({
                  ok: false, diagnostics: 'could not reach the compile service: ' + err.message,
              })));
        },
        js_fetch_json: (urlPtr, callbackId) => {
            fetch(readCString(urlPtr)).then(r => r.text())
                .then(txt => complete('__on_fetch_complete', callbackId, txt))
                .catch(() => complete('__on_fetch_complete', callbackId, '{"examples":[]}'));
        },
        js_run_wasm: (b64Ptr, callbackId) => {
            const b64 = readCString(b64Ptr);
            // Yield to the event loop first so the "running…" status paints.
            setTimeout(() => {
                runUserModule(b64, (out) => complete('__on_run_complete', callbackId, out));
            }, 0);
        },
        js_set_hash_source: (ptr) => {
            const src = readCString(ptr);
            // btoa cannot handle non-Latin1; percent-encode first so any source round-trips.
            location.hash = 'src=' + btoa(unescape(encodeURIComponent(src)));
        },
        // Declared `i64 js_get_hash_source()` in api.sf, so it hands back a BigInt
        // (a raw pointer widened to i64, matching the other playground returns).
        js_get_hash_source: () => {
            const m = location.hash.match(/src=([^&]+)/);
            if (!m) return BigInt(writeCString(''));
            try { return BigInt(writeCString(decodeURIComponent(escape(atob(m[1]))))); }
            catch (e) { return BigInt(writeCString('')); }
        },
    };
}

mount('./app.wasm', '#app', {
    loadWasm: loadUiModule,
    env: playgroundEnv,
    onReady: () => {
        // Cmd/Ctrl+Enter runs the program. Bound here rather than in Saffron
        // because it is a document-level listener on a key combination, and
        // Turmeric's event bindings are per-element.
        window.addEventListener('keydown', (e) => {
            if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') {
                e.preventDefault();
                document.querySelector('.btn-run')?.click();
            }
        });
    },
});
