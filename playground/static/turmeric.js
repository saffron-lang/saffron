// Turmeric Runtime — the single browser runtime for Turmeric apps.
//
// This is the one source of truth for the JS side of a Turmeric app: the DOM
// glue, the event dispatch, the NaN-box marshalling, and the WASM boot. Apps
// consume it two ways, both backed by the same `mount()`:
//
//   1. Directly, as a library:
//        import { mount } from './turmeric.js';
//        mount('./app.wasm', '#app');
//
//   2. Via `turmeric-build`, which emits a tiny app.js shim that calls mount().
//
// An app that needs more than the standard DOM/event/router surface — the
// playground runs *user-submitted* wasm in a second instance, for example —
// passes its extras through `mount`'s options rather than forking this file.
// See the `options` docblock on mount(). Forking the runtime is what let the
// dispatch fix and the NaN-box handle logic drift out of sync across copies;
// the extension seam exists so that never has to happen again.

const decoder = new TextDecoder();
const encoder = new TextEncoder();

// --- NaN-box constants (must match src/runtime/wasm_base_32.ll) ---
const PAYLOAD_MASK = 0x0000FFFFFFFFFFFFn;
const TAG_MASK     = 0xFFFF000000000000n;
const TAG_INT      = 0x7FF9000000000000n;
const TAG_PTR      = 0x7FF8000000000000n;

/**
 * Mount a Turmeric WASM app into the DOM.
 *
 * @param {string} wasmUrl - Path to the .wasm file (ignored if options.loadWasm is given)
 * @param {string} selector - CSS selector for the mount point (e.g. '#app')
 * @param {object} [options]
 * @param {(url:string)=>Promise<ArrayBuffer|Uint8Array>} [options.loadWasm]
 *        Custom loader for the app's own wasm bytes. Defaults to fetch(wasmUrl).
 * @param {(ctx:RuntimeCtx)=>object} [options.env]
 *        Extra env imports for the app module. Receives the runtime context so a
 *        handler can readCString/taggedString/findExport against the live
 *        instance. Merged over the built-ins, so an app can also override one.
 * @param {(s:string)=>void} [options.onLog] - Redirect js_log_* (default console.log).
 * @param {(ctx:RuntimeCtx)=>void} [options.onReady] - Called after _start(), before returning.
 * @returns {Promise<RuntimeCtx>}
 *
 * @typedef {object} RuntimeCtx
 * @property {WebAssembly.Instance} instance
 * @property {(ptr:any)=>string} readCString
 * @property {(str:string)=>bigint} writeCString - raw pointer (as BigInt)
 * @property {(str:string)=>bigint} taggedString - TAG_PTR-boxed Saffron String
 * @property {(name:string)=>Function|null} findExport
 * @property {(callbackId:any)=>void} dispatch
 */
export async function mount(wasmUrl, selector = '#app', options = {}) {
    let instance;
    const handles = [null]; // index 0 = null/unused
    const _eventStack = [];
    const _f64buf = new DataView(new ArrayBuffer(8));

    // DOM handles are `Float` on the Saffron side, so a handle handed back as a
    // bare integer returns to us NaN-box TAG_INT tagged on the next call in.
    // Masking the payload accepts both a tagged handle and a plain integer.
    const handleIndex = (id) => Number(BigInt(id) & PAYLOAD_MASK);
    const allocHandle = (el) => { handles.push(el); return handles.length - 1; };
    const getHandle = (id) => handles[handleIndex(id)] || null;

    // Decode a NaN-boxed i64 to a JS number. A `Float` arrives as raw double
    // bits when non-integral (1.0 -> 0x3FF0...), and as TAG_INT|payload when the
    // producer tagged it, so the two are distinguished by tag, not assumed.
    const valToNumber = (v) => {
        const bits = BigInt(v);
        if ((bits & TAG_MASK) === TAG_INT) return Number(bits & PAYLOAD_MASK);
        _f64buf.setBigUint64(0, bits & 0xFFFFFFFFFFFFFFFFn);
        const d = _f64buf.getFloat64(0);
        return Number.isNaN(d) ? Number(bits & PAYLOAD_MASK) : d;
    };

    // Event callback ids cross back into the wasm as an i64 argument to a
    // `Float`-typed export (__dispatch_event / __dispatch_<name>). Codegen
    // indexes the callback list with __val_untag_int, so the id must arrive
    // NaN-box TAG_INT tagged — a raw BigInt untags to 0, so every event would
    // fire handler index 0 instead of its own. This is the encode side of
    // valToNumber.
    const encodeId = (id) => (BigInt(id) & PAYLOAD_MASK) | TAG_INT;

    const readCString = (ptr) => {
        const p = Number(ptr);
        if (p === 0) return "";
        const b = new Uint8Array(instance.exports.memory.buffer);
        let e = p;
        while (e < b.length && b[e] !== 0) e++;
        return decoder.decode(b.slice(p, e));
    };

    const writeCString = (str) => {
        const bytes = encoder.encode(str + '\0');
        const ptr = instance.exports.malloc(BigInt(bytes.length));
        new Uint8Array(instance.exports.memory.buffer).set(bytes, Number(ptr));
        return ptr;
    };

    // Wrap a raw pointer as a NaN-boxed Saffron String value (TAG_PTR|payload).
    // Exported functions take already-tagged Saffron values as params and nothing
    // on the wasm side tags them, so JS must. On wasm32 malloc is (i64)->i32, so
    // the pointer arrives as a Number — hence the BigInt conversion too.
    const taggedString = (str) => (BigInt(writeCString(str)) & 0xFFFFFFFFn) | TAG_PTR;

    // Exported symbols carry a module prefix (e.g. turmeric_events___dispatch_submit),
    // so look up by bare name first, then by suffix match.
    const _exportCache = {};
    const findExport = (name) => {
        if (_exportCache[name] !== undefined) return _exportCache[name];
        let fn = instance.exports[name] || null;
        if (!fn) {
            for (const key of Object.keys(instance.exports)) {
                if (key === name || key.endsWith('_' + name)) { fn = instance.exports[key]; break; }
            }
        }
        _exportCache[name] = fn;
        return fn;
    };
    // _genericDispatch / _getDispatch resolve lazily via findExport (post-instantiate).
    const genericDispatch = () => findExport('__dispatch_event');

    const log = (s) => { if (options.onLog) options.onLog(s); else console.log(s); };

    // The runtime context handed to option.env handlers (and returned to caller).
    const ctx = {
        get instance() { return instance; },
        readCString, writeCString, taggedString, findExport,
        valToNumber, encodeId, getHandle, allocHandle,
        eventStack: _eventStack,
        dispatch: (callbackId) => {
            const d = genericDispatch();
            if (d) d(encodeId(callbackId));
        },
    };

    // Register a DOM event listener that dispatches into the wasm. Shared by the
    // three add_event variants; typed events route to __dispatch_<name> first.
    const addEvent = (handle, eventPtr, callbackId, typed) => {
        const el = getHandle(handle);
        if (!el) return;
        const name = readCString(eventPtr);
        el.addEventListener(name, (e) => {
            // A form's default submit does a full-page GET, discarding the SPA
            // route change the handler is about to make. Prevent it.
            if (name === 'submit') e.preventDefault();
            _eventStack.push(e);
            try {
                if (typed) {
                    const t = findExport(`__dispatch_${name}`);
                    if (t) { t(encodeId(callbackId), 0n); }
                    else { const d = genericDispatch(); if (d) d(encodeId(callbackId)); }
                } else {
                    const d = genericDispatch();
                    if (d) d(encodeId(callbackId));
                }
            } catch (err) {
                // Ignore null-function errors from nil on_click defaults.
                if (!err.message || !err.message.includes('null function')) throw err;
            }
            _eventStack.pop();
        });
    };

    const readEventField = (fieldPtr) => {
        const e = _eventStack[_eventStack.length - 1];
        if (!e) return undefined;
        let val = e;
        for (const p of readCString(fieldPtr).split('.')) { val = val?.[p]; }
        return val;
    };

    // Built-in env imports. option.env is merged over this, so an app can add or
    // override any of them.
    const builtins = {
        // --- Console I/O ---
        js_log_str: (ptr) => log(readCString(ptr)),
        js_log_int: (n) => log(String(Number(n))),
        js_log_bool: (b) => log(Number(b) ? "true" : "false"),
        js_log_nil: () => log("nil"),

        // --- DOM ---
        js_dom_create_element: (tagPtr) => BigInt(allocHandle(document.createElement(readCString(tagPtr)))),
        js_dom_set_text: (handle, textPtr) => { const el = getHandle(handle); if (el) el.textContent = readCString(textPtr); },
        js_dom_set_attr: (handle, namePtr, valuePtr) => setAttr(getHandle(handle), readCString(namePtr), readCString(valuePtr)),
        js_dom_set_attribute: (handle, namePtr, valuePtr) => setAttr(getHandle(handle), readCString(namePtr), readCString(valuePtr)),
        js_dom_append_child: (p, c) => { const parent = getHandle(p), child = getHandle(c); if (parent && child) parent.appendChild(child); },
        js_dom_remove_child: (p, c) => { const parent = getHandle(p), child = getHandle(c); if (parent && child) parent.removeChild(child); },
        js_dom_insert_before: (p, n, r) => { const parent = getHandle(p), node = getHandle(n), ref = getHandle(r); if (parent && node) parent.insertBefore(node, ref); },
        js_dom_set_inner_html: (handle, htmlPtr) => { const el = getHandle(handle); if (el) el.innerHTML = readCString(htmlPtr); },
        js_dom_query_selector: (selPtr) => { const el = document.querySelector(readCString(selPtr)); return el ? BigInt(allocHandle(el)) : 0n; },
        js_dom_set_property: (handle, propPtr, valuePtr) => { const el = getHandle(handle); if (el) el[readCString(propPtr)] = readCString(valuePtr); },
        js_dom_set_bool_property: (handle, propPtr, value) => { const el = getHandle(handle); if (el) el[readCString(propPtr)] = !!Number(value); },

        // --- Events ---
        js_dom_add_event: (h, ev, cb) => addEvent(h, ev, cb, false),
        js_dom_add_event_listener: (h, ev, cb) => addEvent(h, ev, cb, true),
        js_dom_add_typed_event_listener: (h, ev, cb) => addEvent(h, ev, cb, true),
        js_event_get_float: (_eventPtr, fieldPtr) => {
            const buf = new ArrayBuffer(8);
            new Float64Array(buf)[0] = Number(readEventField(fieldPtr)) || 0;
            return new BigInt64Array(buf)[0]; // NaN-boxed float: raw double bits
        },
        js_event_get_string: (_eventPtr, fieldPtr) => {
            const val = readEventField(fieldPtr);
            // Raw pointer as i64; codegen does inttoptr + tag_ptr on it.
            return BigInt(writeCString(String(val ?? ''))) & 0xFFFFFFFFn;
        },
        js_event_get_bool: (_eventPtr, fieldPtr) => (readEventField(fieldPtr) ? 1n : 0n),
        js_event_prevent_default: () => { const e = _eventStack[_eventStack.length - 1]; if (e) e.preventDefault(); },
        js_event_stop_propagation: () => { const e = _eventStack[_eventStack.length - 1]; if (e) e.stopPropagation(); },

        // --- Memory ---
        memcpy: (dst, src, len) => {
            new Uint8Array(instance.exports.memory.buffer).copyWithin(Number(dst), Number(src), Number(src) + Number(len));
            return dst;
        },

        // --- Navigation / Router FFI ---
        js_set_hash: (ptr) => { location.hash = readCString(ptr); },
        js_get_hash: () => writeCString(location.hash.replace(/^#\/?/, '') || '/'),
        js_push_state: (ptr) => history.pushState(null, '', readCString(ptr)),
        js_get_pathname: () => writeCString(location.pathname),

        // --- Fetch (dispatches to __on_fetch_complete) ---
        js_fetch_json: (urlPtr, cb) => fetchInto(readCString(urlPtr), undefined, cb),
        js_fetch_post: (urlPtr, bodyPtr, cb) =>
            fetchInto(readCString(urlPtr), { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: readCString(bodyPtr) }, cb),
        js_fetch_post_auth: (urlPtr, bodyPtr, tokenPtr, cb) =>
            fetchInto(readCString(urlPtr), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + readCString(tokenPtr) },
                body: readCString(bodyPtr),
            }, cb),

        // --- Timers (dispatch to __on_timeout) ---
        js_set_timeout: (cb, ms) => { const id = setTimeout(() => { const fn = findExport('__on_timeout'); if (fn) fn(encodeId(cb)); }, valToNumber(ms)); return BigInt(id); },
        js_clear_timeout: (timerId) => clearTimeout(handleIndex(timerId)),
        js_set_interval: (cb, ms) => { const id = setInterval(() => { const fn = findExport('__on_timeout'); if (fn) fn(encodeId(cb)); }, valToNumber(ms)); return BigInt(id); },
        js_clear_interval: (timerId) => clearInterval(handleIndex(timerId)),

        // --- System ---
        __string_intern: (ptr) => ptr,
        __builtin_trap: () => { throw new Error("Saffron: exit/trap"); },
    };

    function setAttr(el, name, value) {
        if (!el) return;
        if (name === 'style') el.style.cssText = value;
        else if (name === 'class') el.className = value;
        else el.setAttribute(name, value);
    }

    function fetchInto(url, init, callbackId) {
        fetch(url, init).then(r => r.text()).then(text => {
            const fn = findExport('__on_fetch_complete');
            if (fn) fn(encodeId(callbackId), taggedString(text));
        }).catch((err) => {
            const fn = findExport('__on_fetch_complete');
            if (fn) fn(encodeId(callbackId), taggedString('{"error":"fetch failed: ' + (err && err.message || '') + '"}'));
        });
    }

    // Merge app-supplied env over the built-ins. The catch-all returns a no-op
    // returning 0n for any import the module declares but we don't provide.
    const extraEnv = options.env ? options.env(ctx) : {};
    const env = new Proxy(Object.assign({}, builtins, extraEnv), {
        get(target, prop) { return (prop in target) ? target[prop] : (() => 0n); },
    });

    // Boot: load bytes (custom loader or fetch), instantiate, run _start().
    const wasmBytes = options.loadWasm
        ? await options.loadWasm(wasmUrl)
        : await (await fetch(wasmUrl)).arrayBuffer();
    const result = await WebAssembly.instantiate(wasmBytes, { env });
    instance = result.instance;

    instance.exports._start();

    // Browser navigation -> WASM callback.
    const onNavigate = findExport('__on_navigate');
    if (onNavigate) {
        window.addEventListener('hashchange', () => onNavigate());
        window.addEventListener('popstate', () => onNavigate());
    }

    if (options.onReady) options.onReady(ctx);
    return ctx;
}

// --- Minimal HTML template (for pantry dev) ---
export const DEFAULT_HTML = `<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Turmeric App</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, sans-serif; background: #1a1a2e; color: #e0e0e0; }
        #app { min-height: 100vh; }
    </style>
</head>
<body>
    <div id="app"></div>
    <script type="module">
        import { mount } from './turmeric.js';
        mount('./app.wasm', '#app');
    </script>
</body>
</html>`;
