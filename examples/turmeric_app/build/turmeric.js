// Turmeric Runtime — standard browser entrypoint
// Users import this, never write DOM glue themselves.
//
// Usage:
//   <script type="module">
//     import { mount } from './turmeric.js';
//     mount('./app.wasm', '#app');
//   </script>

const decoder = new TextDecoder();

// DOM handle table — maps integer IDs to real DOM elements
const handles = [null]; // index 0 = null/unused

function allocHandle(el) {
    handles.push(el);
    return handles.length - 1;
}

function getHandle(id) {
    return handles[Number(id)] || null;
}

// Read a null-terminated C string from WASM memory
function readCString(memory, ptr) {
    const p = Number(ptr);
    if (p === 0) return "";
    const bytes = new Uint8Array(memory.buffer);
    let end = p;
    while (end < bytes.length && bytes[end] !== 0) end++;
    return decoder.decode(bytes.slice(p, end));
}

// Detect if a WASM binary uses Memory64 (wasm64) by checking the memory section
function isMemory64(wasmBytes) {
    // Memory64 modules have a memory section with limits flag 0x04
    const view = new Uint8Array(wasmBytes);
    // Simple heuristic: check for the memory64 flag in the first 1KB
    for (let i = 0; i < Math.min(view.length, 1024); i++) {
        if (view[i] === 0x05 && view[i - 1] === 0x04) return true; // memory64 limits
    }
    return false;
}

/**
 * Mount a Turmeric WASM app into the DOM.
 *
 * @param {string} wasmUrl - Path to the .wasm file
 * @param {string} selector - CSS selector for the mount point (e.g. '#app')
 * @param {object} options - Optional configuration
 * @returns {Promise<{instance, dispatch}>} - The WASM instance and event dispatcher
 */
export async function mount(wasmUrl, selector = '#app', options = {}) {
    const response = await fetch(wasmUrl);
    const wasmBytes = await response.arrayBuffer();
    const is64 = isMemory64(wasmBytes);

    let instance;

    // Helper: wrap return values for pointer-returning functions
    const ptrReturn = (val) => is64 ? BigInt(val) : BigInt(val);
    const nullPtr = () => is64 ? 0n : 0n;

    function rs(ptr) {
        return readCString(instance.exports.memory, ptr);
    }

    // Build imports — automatically provides all DOM operations
    const imports = {
        env: new Proxy({
            // --- Console I/O ---
            js_log_str: (ptr) => {
                const s = rs(ptr);
                if (options.onLog) options.onLog(s);
                else console.log(s);
            },
            js_log_int: (n) => {
                const s = String(Number(n));
                if (options.onLog) options.onLog(s);
                else console.log(s);
            },
            js_log_bool: (b) => {
                const s = Number(b) ? "true" : "false";
                if (options.onLog) options.onLog(s);
                else console.log(s);
            },
            js_log_nil: () => {
                if (options.onLog) options.onLog("nil");
                else console.log("nil");
            },

            // --- DOM Operations ---
            js_dom_create_element: (tagPtr) => {
                const tag = rs(tagPtr);
                const el = document.createElement(tag);
                return ptrReturn(allocHandle(el));
            },
            js_dom_set_text: (handle, textPtr) => {
                const el = getHandle(handle);
                if (el) el.textContent = rs(textPtr);
            },
            js_dom_set_attr: (handle, namePtr, valuePtr) => {
                const el = getHandle(handle);
                if (!el) return;
                const name = rs(namePtr);
                const value = rs(valuePtr);
                if (name === 'style') el.style.cssText = value;
                else if (name === 'class') el.className = value;
                else el.setAttribute(name, value);
            },
            js_dom_set_attribute: (handle, namePtr, valuePtr) => {
                const el = getHandle(handle);
                if (!el) return;
                const name = rs(namePtr);
                const value = rs(valuePtr);
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
                if (el) el.innerHTML = rs(htmlPtr);
            },
            js_dom_query_selector: (selectorPtr) => {
                const sel = rs(selectorPtr);
                const el = document.querySelector(sel);
                return el ? ptrReturn(allocHandle(el)) : nullPtr();
            },
            js_dom_add_event_listener: (handle, eventPtr, callbackId) => {
                const el = getHandle(handle);
                if (!el) return;
                const event = rs(eventPtr);
                el.addEventListener(event, (e) => {
                    if (instance.exports.__dispatch_event) {
                        instance.exports.__dispatch_event(callbackId);
                    }
                });
            },
            js_dom_add_event: (handle, eventPtr, callbackId) => {
                const el = getHandle(handle);
                if (!el) return;
                const event = rs(eventPtr);
                el.addEventListener(event, (e) => {
                    if (instance.exports.__dispatch_event) {
                        instance.exports.__dispatch_event(callbackId);
                    }
                });
            },
            js_dom_add_typed_event_listener: (handle, eventPtr, callbackId) => {
                const el = getHandle(handle);
                if (!el) return;
                const event = rs(eventPtr);
                el.addEventListener(event, (e) => {
                    if (instance.exports.__dispatch_event) {
                        instance.exports.__dispatch_event(callbackId);
                    }
                });
            },
            js_dom_set_property: (handle, propPtr, valuePtr) => {
                const el = getHandle(handle);
                if (!el) return;
                el[rs(propPtr)] = rs(valuePtr);
            },
            js_dom_set_bool_property: (handle, propPtr, value) => {
                const el = getHandle(handle);
                if (!el) return;
                el[rs(propPtr)] = !!Number(value);
            },

            // --- Event data access (for typed event handlers) ---
            js_event_get_float: () => nullPtr(),
            js_event_get_string: () => nullPtr(),
            js_event_get_bool: () => nullPtr(),
            js_event_prevent_default: () => {},
            js_event_stop_propagation: () => {},

            // --- Memory operations ---
            memcpy: (dst, src, len) => {
                const b = new Uint8Array(instance.exports.memory.buffer);
                b.copyWithin(Number(dst), Number(src), Number(src) + Number(len));
                return dst;
            },

            // --- System ---
            __builtin_trap: () => { throw new Error("Saffron: unreachable/exit called"); },
        }, {
            // Catch-all: return a no-op function for any import we don't explicitly handle
            get(target, prop) {
                if (prop in target) return target[prop];
                return (...args) => nullPtr();
            }
        }),
    };

    // Instantiate
    const result = await WebAssembly.instantiate(wasmBytes, imports);
    instance = result.instance;

    // Run the app
    instance.exports._start();

    return {
        instance,
        handles,
        getHandle,
        dispatch: (callbackId) => {
            if (instance.exports.__dispatch_event) {
                instance.exports.__dispatch_event(is64 ? BigInt(callbackId) : callbackId);
            }
        },
    };
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
