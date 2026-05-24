interface BuiltinFn {
  name: string;
  params: string;
  returnType: string;
}

interface BuiltinModule {
  name: string;
  functions: BuiltinFn[];
}

const builtins: BuiltinModule[] = [
  {
    name: "IO",
    functions: [
      { name: "print", params: "value: Any", returnType: "Nil" },
      { name: "println", params: "value: Any", returnType: "Nil" },
      { name: "read_file", params: "path: String", returnType: "String" },
      { name: "write_file", params: "path: String, content: String", returnType: "Nil" },
      { name: "append_file", params: "path: String, content: String", returnType: "Nil" },
      { name: "file_exists", params: "path: String", returnType: "Bool" },
      { name: "delete_file", params: "path: String", returnType: "Bool" },
      { name: "readline", params: "", returnType: "String" },
      { name: "list_dir", params: "path: String", returnType: "List<String>" },
      { name: "walk_dir", params: "path: String", returnType: "List<List<Any>>" },
      { name: "mkdir", params: "path: String", returnType: "Bool" },
      { name: "rename", params: "from: String, to: String", returnType: "Bool" },
      { name: "file_size", params: "path: String", returnType: "Number" },
      { name: "is_dir", params: "path: String", returnType: "Bool" },
    ],
  },
  {
    name: "Task",
    functions: [
      { name: "spawn", params: "callback: () => Any", returnType: "Task" },
    ],
  },
  {
    name: "Async",
    functions: [
      { name: "sleep", params: "seconds: Number", returnType: "Nil" },
      { name: "await", params: "task: Task", returnType: "Any" },
    ],
  },
  {
    name: "Json",
    functions: [
      { name: "parse", params: "source: String", returnType: "Any" },
      { name: "parse_into", params: "klass: Any, source: String", returnType: "Any" },
      { name: "to_string", params: "value: Any", returnType: "String" },
    ],
  },
  {
    name: "Reflect",
    functions: [
      { name: "is_number", params: "value: Any", returnType: "Bool" },
      { name: "is_string", params: "value: Any", returnType: "Bool" },
      { name: "is_bool", params: "value: Any", returnType: "Bool" },
      { name: "is_nil", params: "value: Any", returnType: "Bool" },
      { name: "is_list", params: "value: Any", returnType: "Bool" },
      { name: "is_map", params: "value: Any", returnType: "Bool" },
      { name: "is_instance", params: "value: Any", returnType: "Bool" },
      { name: "is_class", params: "value: Any", returnType: "Bool" },
      { name: "fields", params: "instance: Any", returnType: "Map" },
      { name: "class_name", params: "instance: Any", returnType: "String" },
      { name: "type_of", params: "value: Any", returnType: "String" },
      { name: "number_to_string", params: "value: Number", returnType: "String" },
      { name: "construct", params: "klass: Any, fields: Map", returnType: "Any" },
    ],
  },
  {
    name: "Time",
    functions: [
      { name: "clock", params: "", returnType: "Number" },
    ],
  },
  {
    name: "Math",
    functions: [
      { name: "sqrt", params: "x: Number", returnType: "Number" },
      { name: "pow", params: "base: Number, exp: Number", returnType: "Number" },
      { name: "sin", params: "x: Number", returnType: "Number" },
      { name: "cos", params: "x: Number", returnType: "Number" },
      { name: "tan", params: "x: Number", returnType: "Number" },
      { name: "log", params: "x: Number", returnType: "Number" },
      { name: "abs", params: "x: Number", returnType: "Number" },
      { name: "floor", params: "x: Number", returnType: "Number" },
      { name: "ceil", params: "x: Number", returnType: "Number" },
      { name: "round", params: "x: Number", returnType: "Number" },
      { name: "random", params: "", returnType: "Number" },
      { name: "pi", params: "", returnType: "Number" },
    ],
  },
];

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

export const builtinModuleNames = builtins.map((m) => m.name);
