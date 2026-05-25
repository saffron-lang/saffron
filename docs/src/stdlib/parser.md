# Parser

```saffron
import "@parser" as Parser
```

Parse Saffron source code into an AST. Wraps the internal compiler parser for use by tooling.

## Functions

### `parse(source: String): List<Stmt>`

Parse a source string into a list of top-level statements:

```saffron
import "@parser" as Parser

var stmts = Parser.parse("fun hello() { IO.println(\"hi\") }")
IO.println(stmts.length())  // 1
```

### `parse_file(path: String): List<Stmt>`

Read and parse a file:

```saffron
var stmts = Parser.parse_file("src/lib/math.sf")
```

### `parse_expr(source: String): Expr`

Parse a single expression:

```saffron
var expr = Parser.parse_expr("1 + 2 * 3")
```

## Example: extracting all function names

```saffron
import "@parser" as Parser
import "@ast" as AST

var stmts = Parser.parse_file("my_module.sf")
var fns = AST.find_functions(stmts)

for (fn_stmt in fns) {
    IO.println(AST.stmt_name(fn_stmt))
}
```
