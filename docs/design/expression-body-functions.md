# Expression-Body Functions

Kotlin-style `fun name(params): Type = expr` syntax as sugar for single-expression function bodies.

## Syntax

```saffron
fun add(a: Number, b: Number): Number = a + b
fun double(x: Number) = x * 2              // return type inferred

class Vec2 {
    var x: Number
    var y: Number
    fun length(): Number = (this.x * this.x + this.y * this.y).sqrt()
    fun add(other: Vec2): Vec2 = Vec2(this.x + other.x, this.y + other.y)
}
```

Any expression is valid on the RHS, including `if`, `match`, calls, etc.

## Parser change

In `parse_fun_decl_with_doc` (parser.sf ~line 1396), after parsing the return type:

```saffron
// Current:
var body: List<AST.Stmt> = []
if (this.match_kind_check("{")) {
    body = this.parse_block_stmts()
}

// New:
var body: List<AST.Stmt> = []
if (this.match_kind("=")) {
    var expr: AST.Expr = this.parse_expression()
    body = [AST.Stmt.Return(expr)]
} else if (this.match_kind_check("{")) {
    body = this.parse_block_stmts()
}
```

That's the entire parser change (~4 lines).

## Lambda distinction

- **Lambdas** (anonymous): `fun (params) => expr` — uses `=>`
- **Named functions** (declarations): `fun name(params) = expr` — uses `=`

No ambiguity: after `)` or `: Type`, `=` cannot currently appear.

## Type inference

When return type is omitted (`fun f(x: Number) = x * 2`), the checker infers from the body expression. No change needed — already handles this for block bodies.

## Multi-line expressions

Works naturally — `parse_expression()` handles compound expressions:

```saffron
fun abs(x: Number): Number = if (x >= 0) x else -x

fun describe(opt: Option): String = match (opt) {
    Some(v) => "has: ${v}"
    None => "empty"
}
```

## Impact on other passes

| Pass | Change needed |
|------|--------------|
| Codegen | None — sees `[Return(expr)]` |
| Type checker | None — same AST shape |
| AST | None — no new node types |

## Bootstrap concern

Safe. Gen2 compiles the parser code (which uses only existing constructs). Gen2 doesn't need to parse `=`-body syntax itself.

## Estimated effort

~10 lines in `parse_fun_decl_with_doc`. No other files. One-commit feature.
