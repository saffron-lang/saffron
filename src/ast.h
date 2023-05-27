#include "scanner.h"
#include "value.h"

typedef enum {
    NODE_BINARY,
    NODE_GROUPING,
    NODE_LITERAL,
    NODE_UNARY,
    NODE_VARIABLE,
    NODE_ASSIGN,
    NODE_LOGICAL,
    NODE_CALL,
    NODE_GET,
    NODE_SET,
    NODE_SUPER,
    NODE_THIS,
    NODE_YIELD,
    NODE_LAMBDA,
    NODE_EXPRESSION,
    NODE_PRINT,
    NODE_VAR,
    NODE_BLOCK,
    NODE_FUNCTION,
    NODE_CLASS,
    NODE_IF,
    NODE_WHILE,
    NODE_BREAK,
    NODE_RETURN,
    NODE_IMPORT,
} NodeType;
typedef struct {
    NodeType type;
} Expr;

struct Binary {
    Expr left;
    Token operator;
    Expr* right;
};

struct Grouping {
    Expr* expression;
};

struct Literal {
    Value value;
};

struct Unary {
    Token operator;
    Expr* right;
};

struct Variable {
    Token name;
};

struct Assign {
    Token name;
    Expr* value;
};

struct Logical {
    Expr* left;
    Token operator;
    Expr* right;
};

struct Call {
    Expr* callee;
    Token paren;
    Expr* arguments;
    int argCount;
};

struct Get {
    Expr* object;
    Token name;
};

struct Set {
    Expr* object;
    Token name;
    Expr value;
};

struct Super {
    Token keyword;
    Token method;
};

struct This {
    Token keyword;
};

struct Yield {
    Expr* expression;
};

struct Lambda {
    Token* params;
    int paramCount;
    struct Stmt* body;
    int statementCount;
};

typedef struct {
    NodeType type;
} Stmt;

struct Expression {
    Expr expression;
};

struct Print {
    Expr* expression;
};

struct Var {
    Token name;
    Expr* initializer;
};

struct Block {
    Stmt* statements;
    int statementCount;
};

struct Function {
    Token name;
    Token* params;
    int paramCount;
    Stmt* body;
    int statementCount;
};

struct Class {
    Token name;
    struct Variable* superclass;
    struct Function* methods;
    int methodCount;
};

struct If {
    Expr* condition;
    Stmt* thenBranch;
    Stmt* elseBranch;
};

struct While {
    Expr* condition;
    Stmt* body;
};

struct Break {
    Token keyword;
};

struct Return {
    Token keyword;
    Expr* value;
};

struct Import {
    Expr* expression;
};