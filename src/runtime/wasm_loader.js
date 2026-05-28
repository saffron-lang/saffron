// Saffron WASM Loader
// Minimal JS glue to run Saffron-compiled WASM modules in the browser.
// Provides: console I/O, string reading from WASM memory.

export async function loadSaffron(wasmUrl) {
    const memory = new WebAssembly.Memory({ initial: 256, maximum: 65536 });

    function readCString(ptr) {
        const bytes = new Uint8Array(memory.buffer);
        let end = ptr;
        while (bytes[end] !== 0) end++;
        return new TextDecoder().decode(bytes.slice(ptr, end));
    }

    function writeCString(str) {
        const encoder = new TextEncoder();
        const encoded = encoder.encode(str);
        // Allocate in WASM memory via exported malloc
        const ptr = instance.exports.malloc(encoded.length + 1);
        const bytes = new Uint8Array(memory.buffer);
        bytes.set(encoded, ptr);
        bytes[ptr + encoded.length] = 0;
        return ptr;
    }

    const imports = {
        env: {
            memory,

            // --- I/O (called by wasm_base.ll) ---
            js_log_str: (ptr) => {
                console.log(readCString(ptr));
            },
            js_log_int: (n) => {
                console.log(Number(n));
            },
            js_log_bool: (b) => {
                console.log(b ? "true" : "false");
            },
            js_log_nil: () => {
                console.log("nil");
            },

            // --- Trap (exit) ---
            __builtin_trap: () => {
                throw new Error("Saffron: exit called");
            },
        },
    };

    const response = await fetch(wasmUrl);
    const wasmBytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(wasmBytes, imports);

    return {
        instance,
        memory,
        readCString,
        writeCString,

        // Run the Saffron program
        run() {
            if (instance.exports._start) {
                instance.exports._start();
            } else if (instance.exports.__saffron_main) {
                instance.exports.__saffron_main();
            }
        },
    };
}

// --- HTML helper: load and run with a single script tag ---
// <script type="module">
//   import { loadSaffron } from './wasm_loader.js';
//   const app = await loadSaffron('./app.wasm');
//   app.run();
// </script>
