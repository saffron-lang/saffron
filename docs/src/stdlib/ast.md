# AST

```saffron
import "@ast" as AST
```

AST node types and traversal utilities for working with Saffron syntax trees.

## Types

### `Expr`

Expression nodes:

```saffron
enum Expr {
    IntLit(value: Int),
    FloatLit(value: Float),
    BoolLit(value: Bool),
    StringLit(value: String),
    NilLit,
    Variable(name: String),
    Binary(left: Expr, op: String, right: Expr),
    Unary(op: String, right: Expr),
    Call(callee: Expr, args: List<Expr>),
    Assign(name: String, value: Expr),
    Logical(left: Expr, op: String, right: Expr),
    MemberAccess(object: Expr, field: String),
    Match(subject: Expr, arms: List<MatchArm>),
    Lambda(params: String, ret_type: String, body: List<Stmt>),
    GetField(object: Expr, field: String),
    SetField(object: Expr, field: String, value: Expr),
    This,
    ListLit(elements: List<Expr>),
    IndexGet(object: Expr, index: Expr),
    IndexSet(object: Expr, index: Expr, value: Expr),
    MethodCall(object: Expr, method: String, args: List<Expr>),
    MapLit
}
```

### `Stmt`

Statement nodes:

```saffron
enum Stmt {
    ExprStmt(expr: Expr),
    VarDecl(name: String, type_ann: String, initializer: Expr, docstring: String),
    FunDecl(name: String, params: String, ret_type: String, body: List<Stmt>, docstring: String),
    EnumDecl(name: String, variants: List<Variant>, docstring: String),
    ClassDecl(name: String, fields: String, methods: List<Stmt>, docstring: String),
    Return(value: Expr),
    If(condition: Expr, then_branch: List<Stmt>, else_branch: List<Stmt>),
    While(condition: Expr, body: List<Stmt>),
    Block(stmts: List<Stmt>)
}
```

### `Pattern`

Match patterns:

```saffron
enum Pattern {
    VariantPattern(variant: String, bindings: List<String>),
    WildcardPattern,
    LiteralPattern(value: Expr)
}
```

## Functions

### `walk_stmts(stmts, visitor)`

Walk all statements recursively, calling `visitor` on each:

```saffron
import "@parser" as Parser
import "@ast" as AST

var stmts = Parser.parse_file("module.sf")
AST.walk_stmts(stmts, fun (stmt: AST.Stmt) {
    IO.println(stmt)
})
```

### `find_functions(stmts): List<Stmt>`

Extract all function declarations:

```saffron
var fns = AST.find_functions(stmts)
```

### `find_classes(stmts): List<Stmt>`

Extract all class declarations:

```saffron
var classes = AST.find_classes(stmts)
```

### `find_enums(stmts): List<Stmt>`

Extract all enum declarations:

```saffron
var enums = AST.find_enums(stmts)
```

### `stmt_name(stmt): String`

Get the name of a declaration:

```saffron
for (fn_stmt in AST.find_functions(stmts)) {
    IO.println(AST.stmt_name(fn_stmt))
}
```

### `stmt_doc(stmt): String`

Get the docstring of a declaration:

```saffron
for (fn_stmt in AST.find_functions(stmts)) {
    var doc = AST.stmt_doc(fn_stmt)
    if (!(doc == "")) {
        IO.println("${AST.stmt_name(fn_stmt)}: ${doc}")
    }
}
```

## Example: building a documentation tool

```saffron
import "@parser" as Parser
import "@ast" as AST

var stmts = Parser.parse_file("src/lib/math.sf")

IO.println("# math")
IO.println("")

for (fn_stmt in AST.find_functions(stmts)) {
    var name = AST.stmt_name(fn_stmt)
    var doc = AST.stmt_doc(fn_stmt)
    IO.println("## ${name}")
    if (!(doc == "")) {
        IO.println(doc)
    }
    IO.println("")
}
```
