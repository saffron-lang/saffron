export async function loadSaffron(wasmUrl) {
    function readCString(mem, ptr) {
        const bytes = new Uint8Array(mem.buffer);
        let end = ptr;
        while (end < bytes.length && bytes[end] !== 0) end++;
        return new TextDecoder().decode(bytes.slice(ptr, end));
    }

    let instance;
    const imports = {
        env: {
            js_log_str: (ptr) => {
                const s = readCString(instance.exports.memory, ptr);
                console.log(s);
                const el = document.getElementById('output');
                if (el) el.textContent += s + '\n';
            },
            js_log_int: (n) => {
                console.log(Number(n));
                const el = document.getElementById('output');
                if (el) el.textContent += n + '\n';
            },
            js_log_bool: (b) => {
                const s = b ? "true" : "false";
                console.log(s);
                const el = document.getElementById('output');
                if (el) el.textContent += s + '\n';
            },
            js_log_nil: () => {
                console.log("nil");
                const el = document.getElementById('output');
                if (el) el.textContent += 'nil\n';
            },
            __builtin_trap: () => { throw new Error("Saffron: exit called"); },
        },
    };

    const response = await fetch(wasmUrl);
    const wasmBytes = await response.arrayBuffer();
    const result = await WebAssembly.instantiate(wasmBytes, imports);
    instance = result.instance;
    return instance;
}
