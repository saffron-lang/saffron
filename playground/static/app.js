// Saffron Playground loader.
//
// Derived from turmeric/runtime/app_template.js, with two additions the template
// does not have:
//
//   1. `js_run_wasm` — instantiates the *user's* compiled module as a second,
//      completely separate WebAssembly instance and captures everything it prints.
//   2. `fmod` and `js_time_now` — the template's Proxy catch-all returns `0n`
//      (a BigInt) for every missing import, which throws
//      "Cannot convert a BigInt value to a number" the moment a wasm function
//      declared to return f64 imports it. Any program doing float modulo (`%`)
//      or reading the clock hit this. See bug 15 in the bug log.
//
// The UI module and the user module never share memory. Each gets its own
// `WebAssembly.Memory`, its own handle table, and its own import object; the only
// channel between them is the captured output string.

const decoder = new TextDecoder();
const handles = [null];
let instance;

function allocHandle(el) { handles.push(el); return handles.length - 1; }
function getHandle(id) { return handles[Number(id)] || null; }

function readCStringFrom(exports, ptr) {
    const p = Number(ptr);
    if (p === 0) return "";
    const b = new Uint8Array(exports.memory.buffer);
    let e = p;
    while (e < b.length && b[e] !== 0) e++;
    return decoder.decode(b.slice(p, e));
}
function readCString(ptr) { return readCStringFrom(instance.exports, ptr); }

const _eventStack = [];

// ---------------------------------------------------------------------------
// Running a user program
// ---------------------------------------------------------------------------

// Cap on how much a program may print. A `while (true) { IO.println("x") }`
// would otherwise grow a JS string until the tab dies.
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

// Format a NaN-boxed-ish value the way the native runtime would. The wasm
// runtime hands us already-formatted strings for most things, so this only has
// to cope with the js_log_* variants.
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
    // memcpy, math. No DOM, no fetch, no timers-with-side-effects. A program
    // cannot touch the page it is running on.
    const userEnv = {
        js_log_str: (ptr) => emit(userRead(ptr) + "\n"),
        js_log_int: (n) => emit(String(Number(n)) + "\n"),
        js_log_float: (f) => emit(String(Number(f)) + "\n"),
        js_log_bool: (b) => emit((Number(b) ? "true" : "false") + "\n"),
        js_log_nil: () => emit("nil\n"),
        js_print_str: (ptr) => emit(userRead(ptr)),
        memcpy: (dst, src, len) => {
            const b = new Uint8Array(userInstance.exports.memory.buffer);
            b.copyWithin(Number(dst), Number(src), Number(src) + Number(len));
            return dst;
        },
        memset: (dst, val, len) => {
            const b = new Uint8Array(userInstance.exports.memory.buffer);
            b.fill(Number(val), Number(dst), Number(dst) + Number(len));
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
        const bytes = base64ToBytes(b64);
        const mod = await WebAssembly.compile(bytes);

        // Supply a typed zero for anything the module imports that we did not
        // anticipate, rather than a blanket BigInt. Inspecting the declared
        // import list lets us return the right *kind* of zero.
        const needed = WebAssembly.Module.imports(mod);
        for (const imp of needed) {
            if (imp.module !== "env") continue;
            if (userEnv[imp.name] !== undefined) continue;
            userEnv[imp.name] = () => 0;
        }

        const result = await WebAssembly.instantiate(mod, { env: userEnv });
        userInstance = result;

        userInstance.exports._start();

        // Drive the cooperative scheduler. `__sched_pump` runs ready tasks and
        // returns a nonzero value while work remains; without this loop any
        // program using Task.spawn / Async.sleep would print nothing after its
        // first suspension point.
        const pump = userInstance.exports.__sched_pump;
        if (typeof pump === "function") {
            let n = 0;
            while (n < MAX_PUMPS) {
                let more;
                try {
                    more = pump();
                } catch (err) {
                    emit("\n[scheduler error: " + (err.message || String(err)) + "]\n");
                    break;
                }
                if (!Number(more)) break;
                n++;
            }
            if (n >= MAX_PUMPS) {
                emit("\n[stopped: program still had pending tasks after " +
                     MAX_PUMPS + " scheduler steps]\n");
            }
        }
        report(out);
    } catch (err) {
        const message = err && err.message ? err.message : String(err);
        // "!!" tells the UI to render this as an error.
        report("!!" + (out ? out + "\n" : "") + message);
    }
}

// ---------------------------------------------------------------------------
// Imports for the UI module
// ---------------------------------------------------------------------------

const imports = { env: new Proxy({
    js_log_str: (ptr) => console.log(readCString(ptr)),
    js_log_int: (n) => console.log(Number(n)),
    js_log_bool: (b) => console.log(Number(b) ? "true" : "false"),
    js_log_nil: () => console.log("nil"),
    js_dom_create_element: (tagPtr) => {
        return BigInt(allocHandle(document.createElement(readCString(tagPtr))));
    },
    js_dom_set_text: (handle, textPtr) => {
        const el = getHandle(handle);
        if (el) el.textContent = readCString(textPtr);
    },
    js_dom_set_attr: (handle, namePtr, valuePtr) => {
        const el = getHandle(handle);
        if (!el) return;
        const name = readCString(namePtr);
        const value = readCString(valuePtr);
        if (name === 'style') el.style.cssText = value;
        else if (name === 'class') el.className = value;
        else el.setAttribute(name, value);
    },
    js_dom_set_attribute: (handle, namePtr, valuePtr) => {
        const el = getHandle(handle);
        if (!el) return;
        const name = readCString(namePtr);
        const value = readCString(valuePtr);
        if (name === 'style') el.style.cssText = value;
        else if (name === 'class') el.className = value;
        else el.setAttribute(name, value);
    },
    js_dom_append_child: (parentHandle, childHandle) => {
        const parent = getHandle(parentHandle);
        const child = getHandle(childHandle);
        if (parent && child) parent.appendChild(child);
    },
    js_dom_remove_child: (parentHandle, childHandle) => {
        const parent = getHandle(parentHandle);
        const child = getHandle(childHandle);
        if (parent && child) parent.removeChild(child);
    },
    js_dom_insert_before: (parentHandle, nodeHandle, refHandle) => {
        const parent = getHandle(parentHandle);
        const node = getHandle(nodeHandle);
        const ref = getHandle(refHandle);
        if (parent && node) parent.insertBefore(node, ref);
    },
    js_dom_set_inner_html: (handle, htmlPtr) => {
        const el = getHandle(handle);
        if (el) el.innerHTML = readCString(htmlPtr);
    },
    js_dom_query_selector: (selectorPtr) => {
        const el = document.querySelector(readCString(selectorPtr));
        return el ? BigInt(allocHandle(el)) : 0n;
    },
    js_dom_add_event: (handle, eventPtr, callbackId) => {
        const el = getHandle(handle);
        if (!el) return;
        const name = readCString(eventPtr);
        el.addEventListener(name, (e) => {
            if (name === 'submit') e.preventDefault();
            _eventStack.push(e);
            try {
                if (_genericDispatch) _genericDispatch(callbackId);
            } catch (err) {
                if (!err.message || !err.message.includes('null function')) throw err;
            }
            _eventStack.pop();
        });
    },
    js_dom_add_event_listener: (handle, eventPtr, callbackId) => {
        const el = getHandle(handle);
        if (!el) return;
        const name = readCString(eventPtr);
        el.addEventListener(name, (e) => {
            if (name === 'submit') e.preventDefault();
            _eventStack.push(e);
            const typed = _findExport(`__dispatch_${name}`);
            if (typed) { typed(callbackId, 0n); }
            else if (_genericDispatch) { _genericDispatch(callbackId); }
            _eventStack.pop();
        });
    },
    js_dom_set_property: (handle, propPtr, valuePtr) => {
        const el = getHandle(handle);
        if (el) el[readCString(propPtr)] = readCString(valuePtr);
    },
    js_dom_set_bool_property: (handle, propPtr, value) => {
        const el = getHandle(handle);
        if (el) el[readCString(propPtr)] = !!Number(value);
    },
    js_event_get_float: (eventPtr, fieldPtr) => {
        const e = _eventStack[_eventStack.length - 1];
        if (!e) return 0n;
        const field = readCString(fieldPtr);
        let val = e;
        for (const p of field.split('.')) { val = val?.[p]; }
        const buf = new ArrayBuffer(8);
        new Float64Array(buf)[0] = Number(val) || 0;
        return new BigInt64Array(buf)[0];
    },
    js_event_get_string: (eventPtr, fieldPtr) => {
        const e = _eventStack[_eventStack.length - 1];
        if (!e) return 0n;
        const field = readCString(fieldPtr);
        let val = e;
        for (const p of field.split('.')) { val = val?.[p]; }
        return BigInt(writeCString(String(val ?? ''))) & 0xFFFFFFFFn;
    },
    js_event_get_bool: (eventPtr, fieldPtr) => {
        const e = _eventStack[_eventStack.length - 1];
        if (!e) return 0n;
        const field = readCString(fieldPtr);
        let val = e;
        for (const p of field.split('.')) { val = val?.[p]; }
        return val ? 1n : 0n;
    },
    js_event_prevent_default: () => {
        const e = _eventStack[_eventStack.length - 1];
        if (e) e.preventDefault();
    },
    js_event_stop_propagation: () => {
        const e = _eventStack[_eventStack.length - 1];
        if (e) e.stopPropagation();
    },
    memcpy: (dst, src, len) => {
        const b = new Uint8Array(instance.exports.memory.buffer);
        b.copyWithin(Number(dst), Number(src), Number(src) + Number(len));
        return dst;
    },
    // Float-returning math imports. Must not fall through to the Proxy default:
    // it returns a BigInt, and a wasm import declared `-> f64` throws on that.
    fmod: (a, b) => Number(a) % Number(b),
    js_time_now: () => Date.now() / 1000,

    // --- Playground API ---
    js_fetch_post: (urlPtr, bodyPtr, callbackId) => {
        const url = readCString(urlPtr);
        const body = readCString(bodyPtr);
        fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body,
        }).then(r => r.text()).then(txt => {
            const fn = _findExport('__on_fetch_complete');
            if (fn) fn(callbackId, writeCStringI64(txt));
        }).catch((err) => {
            const fn = _findExport('__on_fetch_complete');
            if (fn) fn(callbackId, writeCStringI64(JSON.stringify({
                ok: false,
                diagnostics: 'could not reach the compile service: ' + err.message,
            })));
        });
    },
    js_fetch_json: (urlPtr, callbackId) => {
        const url = readCString(urlPtr);
        fetch(url).then(r => r.text()).then(txt => {
            const fn = _findExport('__on_fetch_complete');
            if (fn) fn(callbackId, writeCStringI64(txt));
        }).catch(() => {
            const fn = _findExport('__on_fetch_complete');
            if (fn) fn(callbackId, writeCStringI64('{"examples":[]}'));
        });
    },
    js_run_wasm: (b64Ptr, callbackId) => {
        const b64 = readCString(b64Ptr);
        // Yield to the event loop first so the "running..." status paints.
        setTimeout(() => {
            runUserModule(b64, (out) => {
                const fn = _findExport('__on_run_complete');
                if (fn) fn(callbackId, writeCStringI64(out));
            });
        }, 0);
    },
    js_set_hash_source: (ptr) => {
        const src = readCString(ptr);
        // btoa cannot handle non-Latin1; percent-encode first so any source
        // (including non-ASCII in strings or comments) round-trips.
        location.hash = 'src=' + btoa(unescape(encodeURIComponent(src)));
    },
    // Declared `i64 js_get_hash_source()` in api.sf, so it must hand back a
    // BigInt even though the wasm32 pointer itself is 32-bit.
    js_get_hash_source: () => {
        const m = location.hash.match(/src=([^&]+)/);
        if (!m) return writeCStringI64('');
        try {
            return writeCStringI64(decodeURIComponent(escape(atob(m[1]))));
        } catch (e) {
            return writeCStringI64('');
        }
    },
    js_set_timeout: (callbackId, ms) => {
        const id = setTimeout(() => {
            const fn = _findExport('__on_timeout');
            if (fn) fn(callbackId);
        }, Number(ms));
        return BigInt(id);
    },
    js_clear_timeout: (timerId) => { clearTimeout(Number(timerId)); },
    __string_intern: (ptr) => ptr,
    __builtin_trap: () => { throw new Error("Saffron: exit/trap"); },
}, { get(t, p) { return t[p] || ((...args) => 0n); } }) };

// Allocate a NUL-terminated string in the module's memory and return the pointer.
//
// Pointer width matters here, twice over. On wasm32 `malloc` is `i8* (i64)`:
// the *argument* is an i64 (so it must be passed as a BigInt) but the *return*
// is a 32-bit pointer, which arrives as a plain JS Number. Returning that
// Number straight back to an import declared `-> i64` (js_get_hash_source is
// one) throws "Cannot convert <n> to a BigInt" and kills _start before the app
// mounts. Normalise in both directions: BigInt going in, Number for indexing,
// and let each caller widen as its own signature requires.
function mallocPtr(byteLength) {
    return Number(instance.exports.malloc(BigInt(byteLength)));
}

function writeCString(str) {
    const bytes = new TextEncoder().encode(str + '\0');
    const ptr = mallocPtr(bytes.length);
    new Uint8Array(instance.exports.memory.buffer).set(bytes, ptr);
    return ptr;
}

// Same string, widened for an import whose declared return type is i64.
function writeCStringI64(str) {
    return BigInt(writeCString(str));
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

// Load the UI module from /api/app_wasm (base64), not /app.wasm (raw bytes).
//
// The Saffron service cannot serve the raw file: `IO.read_file` returns a
// NUL-terminated string and a wasm module starts with `\0asm`, so the body comes
// back empty with `Content-Length: 0` (BUGS.md #66). The base64 endpoint is the
// same workaround the compile path uses for user modules. Fall back to the raw
// path so a plain static file server (`python3 -m http.server` in build/) still
// works for frontend-only development.
async function loadUiModule() {
    const b64Response = await fetch('./api/app_wasm');
    if (b64Response.ok) {
        const text = (await b64Response.text()).trim();
        if (text.length > 0 && text[0] !== '{') return base64ToBytes(text);
    }
    return await (await fetch('./app.wasm')).arrayBuffer();
}

const wasmBytes = await loadUiModule();
const result = await WebAssembly.instantiate(wasmBytes, imports);
instance = result.instance;

// Exported symbols carry a module prefix (e.g. api___on_fetch_complete), so look
// up by bare name first and fall back to a suffix match.
const _exportCache = {};
function _findExport(name) {
    if (_exportCache[name] !== undefined) return _exportCache[name];
    let fn = instance.exports[name] || null;
    if (!fn) {
        for (const key of Object.keys(instance.exports)) {
            if (key === name || key.endsWith('_' + name)) { fn = instance.exports[key]; break; }
        }
    }
    _exportCache[name] = fn;
    return fn;
}

const _genericDispatch = _findExport('__dispatch_event');

instance.exports._start();

// Cmd/Ctrl+Enter runs the program. Bound here rather than in Saffron because it
// is a document-level listener on a key combination, and Turmeric's event
// bindings are per-element.
window.addEventListener('keydown', (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') {
        e.preventDefault();
        document.querySelector('.btn-run')?.click();
    }
});
