// Signatures for the modules that are implicitly available without an import
// (IO, Math, Json, Async, Reflect, Time, Task).
//
// The `builtins` array between the @generated markers below is PRODUCED BY A
// SCRIPT from the real stdlib sources in src/lib/*.sf. Do not hand-edit it —
// regenerate instead:
//
//     node editors/shared/tools/gen-builtins.mjs
//
// Everything outside the markers (the interfaces and the exported helpers) is
// hand-maintained and is never touched by the generator.
//
// `Task` is the only entry with no .sf source — it is implemented natively in
// the compiler/runtime, so its signatures stay hand-written inside the block
// and are carried across regenerations verbatim.

interface BuiltinFn {
  name: string;
  params: string;
  returnType: string;
}

interface BuiltinModule {
  name: string;
  functions: BuiltinFn[];
}

// @generated-start (gen-builtins.mjs) — do not edit by hand
const builtins: BuiltinModule[] = [
  {
    name: "IO",
    functions: [
      { name: "load8", params: "addr: Int", returnType: "Int" },
      { name: "store8", params: "addr: Int, val: Int", returnType: "Nil" },
      { name: "bytes_alloc", params: "size: Int", returnType: "Bytes" },
      { name: "bytes_from_string", params: "s: String", returnType: "Bytes" },
      { name: "open", params: "path: String, mode: String", returnType: "File" },
      { name: "println", params: "value: Any", returnType: "Nil" },
      { name: "println_str", params: "value: String", returnType: "Nil" },
      { name: "print", params: "value: String", returnType: "Nil" },
      { name: "read_file", params: "path: String", returnType: "String" },
      { name: "read_file_bytes", params: "path: String", returnType: "Bytes" },
      { name: "write_file_bytes", params: "path: String, data: Bytes", returnType: "Nil" },
      { name: "file_size", params: "path: String", returnType: "Int" },
      { name: "read_binary", params: "path: String, buf: Int, max_size: Int", returnType: "Int" },
      { name: "write_file", params: "path: String, content: String", returnType: "Nil" },
      { name: "append_file", params: "path: String, content: String", returnType: "Nil" },
      { name: "file_exists", params: "path: String", returnType: "Bool" },
      { name: "mkdir", params: "path: String", returnType: "Nil" },
      { name: "walk_dir", params: "path: String", returnType: "List<String>" },
      { name: "list_dir", params: "path: String", returnType: "List<String>" },
      { name: "is_dir", params: "path: String", returnType: "Bool" },
      { name: "rename", params: "from: String, to: String", returnType: "Bool" },
      { name: "delete_file", params: "path: String", returnType: "Bool" },
    ],
  },
  {
    name: "Task",
    // Native: no src/lib/*.sf source. Hand-maintained; the
    // generator copies this entry through unchanged.
    functions: [
      { name: "spawn", params: "callback: () => Any", returnType: "Task" },
    ],
  },
  {
    name: "Async",
    functions: [
      { name: "sleep", params: "duration: Float", returnType: "Nil" },
      { name: "await", params: "task: Int", returnType: "Any" },
      { name: "gather", params: "tasks: List<Int>", returnType: "List<Int>" },
      { name: "race", params: "tasks: List<Int>", returnType: "Int" },
      { name: "timeout", params: "fn: Fun, seconds: Float", returnType: "Int" },
      { name: "parallel", params: "fns: List<Fun>, max_concurrent: Int", returnType: "List<Int>" },
    ],
  },
  {
    name: "Json",
    functions: [
      { name: "parse", params: "source: String", returnType: "Any" },
      { name: "escape_string", params: "str: String", returnType: "String" },
      { name: "to_string", params: "value: Any", returnType: "String" },
      { name: "parse_into", params: "klass: Any, source: String", returnType: "Any" },
      { name: "pretty", params: "value: Any", returnType: "String" },
    ],
  },
  {
    name: "Reflect",
    functions: [
      { name: "type_name", params: "value: Any", returnType: "String" },
      { name: "is_instance", params: "value: Any", returnType: "Bool" },
      { name: "fields", params: "instance: Any", returnType: "Map<String, Any>" },
      { name: "construct", params: "klass: Any, data: Map<String, Any>", returnType: "Any" },
      { name: "number_to_string", params: "value: Any", returnType: "String" },
    ],
  },
  {
    name: "Time",
    functions: [
      { name: "clock", params: "", returnType: "Float" },
      { name: "timestamp", params: "", returnType: "Float" },
      { name: "sleep", params: "seconds: Float", returnType: "Nil" },
      { name: "elapsed", params: "start: Float", returnType: "Float" },
      { name: "measure", params: "func: () => Any", returnType: "Float" },
      { name: "year", params: "ts: Float = -1", returnType: "Int" },
      { name: "month", params: "ts: Float = -1", returnType: "Int" },
      { name: "day", params: "ts: Float = -1", returnType: "Int" },
      { name: "hour", params: "ts: Float = -1", returnType: "Int" },
      { name: "minute", params: "ts: Float = -1", returnType: "Int" },
      { name: "second", params: "ts: Float = -1", returnType: "Int" },
      { name: "to_iso", params: "ts: Float = -1", returnType: "String" },
      { name: "now", params: "", returnType: "Map<String, Int>" },
    ],
  },
  {
    name: "Math",
    functions: [
      { name: "abs", params: "x: Float", returnType: "Float" },
      { name: "floor", params: "x: Float", returnType: "Float" },
      { name: "ceil", params: "x: Float", returnType: "Float" },
      { name: "round", params: "x: Float", returnType: "Float" },
      { name: "min", params: "a: Float, b: Float", returnType: "Float" },
      { name: "max", params: "a: Float, b: Float", returnType: "Float" },
      { name: "sqrt", params: "x: Float", returnType: "Float" },
      { name: "pow", params: "x: Float, y: Float", returnType: "Float" },
      { name: "sin", params: "x: Float", returnType: "Float" },
      { name: "cos", params: "x: Float", returnType: "Float" },
      { name: "tan", params: "x: Float", returnType: "Float" },
      { name: "asin", params: "x: Float", returnType: "Float" },
      { name: "acos", params: "x: Float", returnType: "Float" },
      { name: "atan", params: "x: Float", returnType: "Float" },
      { name: "atan2", params: "y: Float, x: Float", returnType: "Float" },
      { name: "log", params: "x: Float", returnType: "Float" },
      { name: "log2", params: "x: Float", returnType: "Float" },
      { name: "log10", params: "x: Float", returnType: "Float" },
      { name: "sign", params: "x: Float", returnType: "Float" },
      { name: "clamp", params: "x: Float, lo: Float, hi: Float", returnType: "Float" },
      { name: "lerp", params: "a: Float, b: Float, t: Float", returnType: "Float" },
      { name: "deg_to_rad", params: "degrees: Float", returnType: "Float" },
      { name: "rad_to_deg", params: "radians: Float", returnType: "Float" },
      { name: "hypot", params: "x: Float, y: Float", returnType: "Float" },
      { name: "map_range", params: "value: Float, in_min: Float, in_max: Float, out_min: Float, out_max: Float", returnType: "Float" },
      { name: "is_close", params: "a: Float, b: Float, tolerance: Float", returnType: "Bool" },
      { name: "sum", params: "values: List<Float>", returnType: "Float" },
      { name: "avg", params: "values: List<Float>", returnType: "Float" },
    ],
  },
];
// @generated-end

export function getBuiltinStub(moduleName: string): string | null {
  const mod = builtins.find((m) => m.name === moduleName);
  if (!mod) return null;

  let stub = `// ${mod.name} (native module)\n\n`;
  for (const fn of mod.functions) {
    stub += `fun ${fn.name}(${fn.params}): ${fn.returnType} { /* native */ }\n`;
  }
  return stub;
}

export function getBuiltinFunctionLine(moduleName: string, fnName: string): number | null {
  const mod = builtins.find((m) => m.name === moduleName);
  if (!mod) return null;

  const idx = mod.functions.findIndex((f) => f.name === fnName);
  if (idx === -1) return null;

  // Line 1 = comment, line 2 = blank, line 3+ = functions (0-indexed: line 2+)
  return idx + 2;
}

export function isBuiltinModule(name: string): boolean {
  return builtins.some((m) => m.name === name);
}

/// The functions a builtin module exposes, or [] if the module is unknown.
/// Used by completion to offer `Module.<member>` candidates without shelling
/// out to the compiler (native modules have no .sf source to check).
export function getBuiltinFunctions(moduleName: string): BuiltinFn[] {
  const mod = builtins.find((m) => m.name === moduleName);
  return mod ? mod.functions : [];
}

export const builtinModuleNames = builtins.map((m) => m.name);
