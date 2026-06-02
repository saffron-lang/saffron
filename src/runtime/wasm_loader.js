// Saffron WASM64 Loader
// JS glue to run Saffron-compiled WASM64 (Memory64) modules.
// Provides: console I/O, string reading from WASM memory.
// Requires: Node.js v22+ with --experimental-wasm-memory64, or browser with Memory64 support.

export async function loadSaffron(wasmUrl, options = {}) {
    function readCString(memory, ptr) {
        const bytes = new Uint8Array(memory.buffer);
        let end = Number(ptr);
        while (end < bytes.length && bytes[end] !== 0) end++;
        return new TextDecoder().decode(bytes.slice(Number(ptr), end));
    }

    function writeCString(memory, mallocFn, str) {
        const encoder = new TextEncoder();
        const encoded = encoder.encode(str);
        const ptr = mallocFn(BigInt(encoded.length + 1));
        const bytes = new Uint8Array(memory.buffer);
        bytes.set(encoded, Number(ptr));
        bytes[Number(ptr) + encoded.length] = 0;
        return ptr;
    }

    let instance;

    // Output callback — defaults to console.log, can be overridden
    const output = options.output || ((s) => console.log(s));

    const imports = {
        env: {
            // --- I/O (called by wasm_base.ll) ---
            // In Memory64 mode, pointers are BigInt (i64)
            js_log_str: (ptr) => {
                const s = readCString(instance.exports.memory, ptr);
                output(s);
            },
            js_log_int: (n) => {
                output(String(Number(n)));
            },
            js_log_bool: (b) => {
                output(b ? "true" : "false");
            },
            js_log_nil: () => {
                output("nil");
            },

            // --- Trap (exit) ---
            __builtin_trap: () => {
                throw new Error("Saffron: exit called");
            },

            // --- DOM stubs (no-op outside browser) ---
            js_dom_create_element: () => 0n,
            js_dom_set_text: () => {},
            js_dom_set_attribute: () => {},
            js_dom_append_child: () => {},
            js_dom_remove_child: () => {},
            js_dom_set_inner_html: () => {},
            js_dom_query_selector: () => 0n,
            js_dom_add_event_listener: () => {},
        },
    };

    // Support both fetch (browser) and readFile (Node.js) loading
    let wasmBytes;
    if (typeof wasmUrl === 'string' && typeof process !== 'undefined' && process.versions && process.versions.node) {
        // Node.js: use fs to read the file
        const { readFile } = await import('fs/promises');
        wasmBytes = await readFile(wasmUrl);
    } else if (typeof wasmUrl === 'string' && typeof fetch === 'function') {
        // Browser: use fetch
        const response = await fetch(wasmUrl);
        wasmBytes = await response.arrayBuffer();
    } else if (typeof wasmUrl === 'string') {
        // Fallback: try fs
        const { readFile } = await import('fs/promises');
        wasmBytes = await readFile(wasmUrl);
    } else {
        // Already a buffer
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
// Usage: node --experimental-wasm-memory64 wasm_loader.js <file.wasm>
if (typeof process !== 'undefined' && process.argv && process.argv[1] &&
    (process.argv[1].endsWith('wasm_loader.js') || process.argv[1].endsWith('wasm_loader.mjs'))) {
    const wasmPath = process.argv[2];
    if (!wasmPath) {
        console.error("Usage: node --experimental-wasm-memory64 wasm_loader.js <file.wasm>");
        process.exit(1);
    }
    const app = await loadSaffron(wasmPath);
    app.run();
}
