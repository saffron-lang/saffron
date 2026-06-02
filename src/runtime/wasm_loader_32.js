// Saffron WASM32 Loader
// JS glue to run Saffron-compiled WASM32 modules.
// No BigInt needed — wasm32 uses regular numbers for pointers.
// Provides: console I/O, string reading from WASM memory, memcpy.

export async function loadSaffron(wasmUrl, options = {}) {
    function readCString(memory, ptr) {
        const bytes = new Uint8Array(memory.buffer);
        let end = ptr;
        while (end < bytes.length && bytes[end] !== 0) end++;
        return new TextDecoder().decode(bytes.slice(ptr, end));
    }

    function writeCString(memory, mallocFn, str) {
        const encoder = new TextEncoder();
        const encoded = encoder.encode(str);
        const ptr = mallocFn(encoded.length + 1);
        const bytes = new Uint8Array(memory.buffer);
        bytes.set(encoded, ptr);
        bytes[ptr + encoded.length] = 0;
        return ptr;
    }

    let instance;

    // Output callback — defaults to console.log, can be overridden
    const output = options.output || ((s) => console.log(s));

    const imports = {
        env: {
            // --- I/O (called by wasm_base_32.ll) ---
            // In wasm32, pointers are regular i32 numbers
            js_log_str: (ptr) => {
                const s = readCString(instance.exports.memory, ptr);
                output(s);
            },
            js_log_int: (n_lo, n_hi) => {
                // i64 params are split into two i32 values in wasm32
                // Reconstruct the i64 value
                const val = (BigInt(n_hi >>> 0) << 32n) | BigInt(n_lo >>> 0);
                output(String(Number(val)));
            },
            js_log_bool: (b_lo, b_hi) => {
                output(b_lo ? "true" : "false");
            },
            js_log_nil: () => {
                output("nil");
            },

            // --- Trap (exit) ---
            __builtin_trap: () => {
                throw new Error("Saffron: exit called");
            },

            // --- Memory helpers ---
            memcpy: (dst, src, len) => {
                const bytes = new Uint8Array(instance.exports.memory.buffer);
                bytes.copyWithin(dst, src, src + len);
                return dst;
            },

            // --- DOM stubs (no-op outside browser) ---
            js_dom_create_element: () => 0,
            js_dom_set_text: () => {},
            js_dom_set_attribute: () => {},
            js_dom_append_child: () => {},
            js_dom_remove_child: () => {},
            js_dom_set_inner_html: () => {},
            js_dom_query_selector: () => 0,
            js_dom_add_event_listener: () => {},
        },
    };

    // Support both fetch (browser) and readFile (Node.js) loading
    let wasmBytes;
    if (typeof wasmUrl === 'string' && typeof process !== 'undefined' && process.versions && process.versions.node) {
        const { readFile } = await import('fs/promises');
        wasmBytes = await readFile(wasmUrl);
    } else if (typeof wasmUrl === 'string' && typeof fetch === 'function') {
        const response = await fetch(wasmUrl);
        wasmBytes = await response.arrayBuffer();
    } else if (typeof wasmUrl === 'string') {
        const { readFile } = await import('fs/promises');
        wasmBytes = await readFile(wasmUrl);
    } else {
        wasmBytes = wasmUrl;
    }

    const { instance: inst } = await WebAssembly.instantiate(wasmBytes, imports);
    instance = inst;

    return {
        instance,
        get memory() { return instance.exports.memory; },
        readCString: (ptr) => readCString(instance.exports.memory, ptr),
        writeCString: (str) => writeCString(instance.exports.memory, instance.exports.malloc, str),

        // Run the Saffron program
        run() {
            if (instance.exports._start) {
                instance.exports._start();
            } else if (instance.exports.__saffron_entry) {
                instance.exports.__saffron_entry();
            }
        },
    };
}

// --- Node.js CLI helper ---
// Usage: node wasm_loader_32.js <file.wasm>
if (typeof process !== 'undefined' && process.argv && process.argv[1] &&
    (process.argv[1].endsWith('wasm_loader_32.js') || process.argv[1].endsWith('wasm_loader_32.mjs'))) {
    const wasmPath = process.argv[2];
    if (!wasmPath) {
        console.error("Usage: node wasm_loader_32.js <file.wasm>");
        process.exit(1);
    }
    const app = await loadSaffron(wasmPath);
    app.run();
}
