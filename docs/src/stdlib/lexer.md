# Lexer

```saffron
import "@lexer" as Lexer
```

Tokenize Saffron source code. Wraps the internal compiler lexer for use by tooling.

## Functions

### `tokenize(source: String): List<Token>`

Tokenize a source string:

```saffron
import "@lexer" as Lexer

var tokens = Lexer.tokenize("var x = 42")
for (tok in tokens) {
    IO.println(tok)
}
```

### `tokenize_file(path: String): List<Token>`

Read and tokenize a file:

```saffron
var tokens = Lexer.tokenize_file("src/lib/math.sf")
```

## Types

### `Token`

```saffron
enum Token {
    Token(kind: TokenKind, line: Int, col: Int)
}
```

### `TokenKind`

All possible token types:

```saffron
enum TokenKind {
    // Literals
    TkInt(value: Int),
    TkFloat(value: Float),
    TkString(value: String),
    TkTrue, TkFalse, TkNil,

    // Identifiers & keywords
    TkIdent(name: String),
    TkVar, TkLet, TkFun, TkReturn,
    TkIf, TkElse, TkWhile, TkFor, TkIn,
    TkClass, TkEnum, TkMatch, TkImport, TkAs,
    TkAnd, TkOr, TkThis, TkSuper,
    TkBreak, TkContinue, TkYield,
    TkThrow, TkTry, TkCatch, TkFinally,
    TkInterface, TkExtends, TkIs,

    // Operators
    TkPlus, TkMinus, TkStar, TkSlash, TkPercent,
    TkBang, TkBangEq, TkEq, TkEqEq,
    TkLt, TkLtEq, TkGt, TkGtEq,
    TkAmpersand, TkPipe, TkCaret, TkTilde,
    TkShiftLeft, TkShiftRight,
    TkPipeGt,   // |>
    TkArrow,    // ->
    TkFatArrow, // =>

    // Delimiters
    TkLParen, TkRParen,
    TkLBrace, TkRBrace,
    TkLBracket, TkRBracket,
    TkComma, TkDot, TkColon, TkSemicolon,

    // Documentation
    TkDocComment(text: String),
    TkModuleDoc(text: String),

    // Special
    TkNewline, TkEof
}
```

## Example: counting functions in a file

```saffron
import "@lexer" as Lexer
import "@iter" as Iter

var tokens = Lexer.tokenize_file("my_module.sf")
var fun_count = Iter.count(tokens, fun (tok: Lexer.Token): Bool {
    return match (tok) {
        Token(kind, line, col) => match (kind) {
            TkFun => true
        }
    }
})
IO.println("Found ${fun_count} function declarations")
```
