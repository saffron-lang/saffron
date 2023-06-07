#include "astparse.h"

#include "ast.h"

#include <stdio.h>

#include "../common.h"
#include "../scanner.h"
#include "../object.h"
#include "../memory.h"

#ifdef DEBUG_PRINT_CODE

#include "../debug.h"
#include "../types.h"

#endif

#include <stdlib.h>
#include <string.h>

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    Node *nodes;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_YIELD,       // yield |>
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef Expr *(*ParseFn)(bool canAssign);

typedef Expr *(*InfixParseFn)(Expr *left, bool canAssign);

typedef struct {
    ParseFn prefix;
    InfixParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    bool hasSuperclass;
} ClassCompiler;

Parser parser;

Node *allocateNode(size_t size, NodeType type) {
    Node *node = (Node *) reallocate(NULL, 0, size);
    node->type = type;
    node->isMarked = false;
    node->next = parser.nodes;
    parser.nodes = node;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void *) node, size, type);
#endif

    return node;
}

static void errorAt(Token *token, const char *message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char *message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char *message) {
    errorAt(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char *message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static Stmt *statement();

static Stmt *declaration();

static Expr *expression();

static Value identifierConstant(Token *name);

static ParseRule *getRule(TokenType type);

static Expr *parsePrecedence(Precedence precedence);

static Expr *number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    struct Literal *result = ALLOCATE_NODE(struct Literal, NODE_LITERAL);
    result->value = NUMBER_VAL(value);
    return result;
}

static Expr *unary(bool canAssign) {
    // Parse the operand.
    Token operator = parser.previous;
    Expr *expr = parsePrecedence(PREC_UNARY);

    struct Unary *result = ALLOCATE_NODE(struct Unary, NODE_UNARY);
    result->operator = operator;
    result->right = expr;

    return result;
}


static Expr *list(bool canAssign) {
    uint8_t argCount = 0;
    ExprArray items;
    initExprArray(&items);
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            if (parser.current.type == TOKEN_RIGHT_BRACKET) {
                break;
            }
            Expr *item = expression();
            writeExprArray(&items, item);
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after list items.");

    struct List *result = ALLOCATE_NODE(struct List, NODE_LIST);
    result->items = items;
    return result;
}

static Expr *binary(Expr *left, bool canAssign) {
    Token operator = parser.previous;
    ParseRule *rule = getRule(operator.type);
    Expr *right = parsePrecedence((Precedence) (rule->precedence + 1));

    struct Binary *result = ALLOCATE_NODE(struct Binary, NODE_BINARY);
    result->operator = operator;
    result->right = right;
    result->left = left;

    return result;
}

static Expr *grouping(bool canAssign) {
    Expr *expr = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return expr;
}

static Expr *string(bool canAssign) {
    Value value = (OBJ_VAL(copyString(parser.previous.start + 1,
                                      parser.previous.length - 2)));

    struct Literal *result = ALLOCATE_NODE(struct Literal, NODE_LITERAL);
    result->value = value;
    return result;
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static Expr *variable(bool canAssign) {
    Token name = parser.previous;
    Token next = parser.current;
    if (canAssign && match(TOKEN_EQUAL)) {
        struct Assign *var = ALLOCATE_NODE(struct Assign, NODE_ASSIGN);
        var->name = name;
        var->value = expression();
        return (Expr *) var;
    } else {
        struct Variable *var = ALLOCATE_NODE(struct Variable, NODE_VARIABLE);
        var->name = name;
        return (Expr *) var;
    }
}

static Expr *atom(bool canAssign) {
    ObjAtom *key = copyAtom(parser.previous.start + 1,
                            parser.previous.length - 1);
    struct Literal *result = ALLOCATE_NODE(struct Literal, NODE_LITERAL);
    result->value = OBJ_VAL(key);
    return result;
}

static Expr *literal(bool canAssign) {
    struct Literal *result = ALLOCATE_NODE(struct Literal, NODE_LITERAL);

    switch (parser.previous.type) {
        case TOKEN_FALSE:
            result->value = BOOL_VAL(false);
            break;
        case TOKEN_NIL:
            result->value = NIL_VAL;
            break;
        case TOKEN_TRUE:
            result->value = BOOL_VAL(true);
            break;
    }

    return result;
}

static Expr *and_(Expr *left, bool canAssign) {
    struct Binary *result = ALLOCATE_NODE(struct Binary, NODE_BINARY);
    result->operator = parser.previous;
    result->right = parsePrecedence(PREC_AND);
    result->left = left;// TODO
    return result;
}

static Expr *or_(Expr *left, bool canAssign) {
    struct Binary *result = ALLOCATE_NODE(struct Binary, NODE_BINARY);
    result->operator = parser.previous;
    result->right = parsePrecedence(PREC_OR);
    result->left = left;
    return result;
}

static ExprArray argumentList() {
    ExprArray items;
    initExprArray(&items);

    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (parser.current.type == TOKEN_RIGHT_PAREN) {
                break;
            }
            Expr *expr = expression();
            writeExprArray(&items, expr);
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return items;
}

static Expr *call(Expr *left, bool canAssign) {
    ExprArray arguments = argumentList();
    struct Call *result = ALLOCATE_NODE(struct Call, NODE_CALL);
    result->paren = parser.previous;
    result->arguments = arguments;
    result->callee = left;
    return (Expr *) result;
}

static Expr *getItem(Expr *left, bool canAssign) {
    Expr *expr = expression();
    struct GetItem *result = ALLOCATE_NODE(struct GetItem, NODE_GETITEM);
    result->object = left;
    result->index = expr;
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
    return (Expr *) result;
}

static Expr *pipeCall(Expr *left, bool canAssign) {
    Expr *expr = parsePrecedence(PREC_ASSIGNMENT + 1);
    struct Binary *result = ALLOCATE_NODE(struct Binary, NODE_BINARY);
    result->operator = parser.previous;
    result->right = expr;
    result->left = left;
    return (Expr *) result;
}

static Expr *dot(Expr *left, bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    Token name = parser.previous;

    if (match(TOKEN_EQUAL)) {
        struct Set *result = ALLOCATE_NODE(struct Set, NODE_SET);
        result->object = left;
        result->name = name;
        result->value = expression();
        return result;
    }

    struct Get *result = ALLOCATE_NODE(struct Get, NODE_GET);
    result->object = left;
    result->name = name;
    return result;
}

static Expr *this_(bool canAssign) {
    return variable(false);
}

static Token syntheticToken(const char *text) {
    Token token;
    token.start = text;
    token.length = (int) strlen(text);
    return token;
}

static Expr *super_(bool canAssign) {
    struct Super *result = ALLOCATE_NODE(struct Super, NODE_SUPER);
    result->keyword = parser.previous;

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");

    Token name = parser.previous;
    result->keyword = name;
    result->method = parser.previous;
    return result;
}

static Expr *yield(bool canAssign) {
    struct Yield *result = ALLOCATE_NODE(struct Yield, NODE_YIELD);
    if (!check(TOKEN_SEMICOLON)) {
        result->expression = parsePrecedence(PREC_YIELD);
    }
    return result;
}

static Expr *anonFunction(bool canAssign);

ParseRule parseRules[] = {
        [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_CALL},
        [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
        [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
        [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
        [TOKEN_LEFT_BRACKET]  = {list, getItem, PREC_CALL},
        [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
        [TOKEN_PIPE]          = {NULL, pipeCall, PREC_YIELD},
        [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
        [TOKEN_DOT]           = {NULL, dot, PREC_CALL},
        [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
        [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
        [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
        [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
        [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
        [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
        [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQUALITY},
        [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
        [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
        [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
        [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
        [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
        [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
        [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
        [TOKEN_ATOM]          = {atom, NULL, PREC_NONE},
        [TOKEN_STRING]        = {string, NULL, PREC_NONE},
        [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
        [TOKEN_AND]           = {NULL, and_, PREC_AND},
        [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
        [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
        [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
        [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
        [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
        [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
        [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
        [TOKEN_OR]            = {NULL, or_, PREC_OR},
        [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
        [TOKEN_SUPER]         = {super_, NULL, PREC_NONE},
        [TOKEN_THIS]          = {this_, NULL, PREC_NONE},
        [TOKEN_TRUE]          = {literal, NULL, PREC_NONE},
        [TOKEN_VAR]           = {NULL, NULL, PREC_NONE},
        [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
        [TOKEN_YIELD]         = {yield, NULL, PREC_NONE},
        [TOKEN_AWAIT]         = {NULL, NULL, PREC_NONE},
        [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
        [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

static ParseRule *getRule(TokenType type) {
    return &parseRules[type];
}

static Expr *parsePrecedence(Precedence precedence) {
    advance();
    TokenType type = parser.previous.type;
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return NULL;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    Expr *result = prefixRule(canAssign);

    Token last = parser.previous;
    Token current = parser.current;

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        InfixParseFn infixRule = getRule(parser.previous.type)->infix;
        result = infixRule(result, canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }

    return result;
}


static Token parseVariable(const char *errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    return parser.previous;
}

static void beginScope();

static Stmt *block();

static TypeNode *typeAnnotation();

static Expr *anonFunction(bool canAssign) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after fun keyword.");
    TokenArray tokens;
    initTokenArray(&tokens);

    TypeNodeArray types;
    initTypeNodeArray(&types);

    int argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            argCount++;
            if (argCount > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            Token name = parseVariable("Expect parameter name.");
            writeTokenArray(&tokens, name, name.line);

            if (match(TOKEN_COLON)) {
                writeTypeNodeArray(&types, typeAnnotation());
            } else {
                writeTypeNodeArray(&types, NULL);
            }
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_ARROW, "Expect '=>' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    struct Block *bl = (struct Block *) block();
    struct Lambda *result = ALLOCATE_NODE(struct Lambda, NODE_LAMBDA);
    result->body = bl->statements;
    result->params = tokens;
    result->paramTypes = types;

    result->self.type = (TypeNode *) initFunctor(types, NULL);
    return result;
}

static Expr *expression() {
    if (match(TOKEN_FUN)) {
        return anonFunction(false);
    }
    return parsePrecedence(PREC_ASSIGNMENT);
}

static Stmt *expressionStatement() {
    struct Expression *result = ALLOCATE_NODE(struct Expression, NODE_EXPRESSION);
    result->self.self.lineno = parser.current.line;
    Expr *expr = expression();
    match(TOKEN_SEMICOLON);
    result->expression = expr;
    return result;
}

static Stmt *block() {
    StmtArray stmts;
    initStmtArray(&stmts);
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        writeStmtArray(&stmts, declaration());
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");

    struct Block *result = ALLOCATE_NODE(struct Block, NODE_BLOCK);
    result->statements = stmts;
    return result;
}

static struct Function *function(FunctionType type) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    TokenArray tokens;
    initTokenArray(&tokens);

    TypeNodeArray types;
    initTypeNodeArray(&types);

    int argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            argCount++;
            if (argCount > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            Token name = parseVariable("Expect parameter name.");
            writeTokenArray(&tokens, name, name.line);

            if (match(TOKEN_COLON)) {
                writeTypeNodeArray(&types, typeAnnotation());
            } else {
                writeTypeNodeArray(&types, NULL);
            }
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    TypeNode *returnType = NULL;
    if (match(TOKEN_COLON)) {
        returnType = typeAnnotation();
    }

    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    struct Block *body = (struct Block *) block();
    struct Function *result = ALLOCATE_NODE(struct Function, NODE_FUNCTION);
    result->body = body->statements;
    result->params = tokens;
    result->paramTypes = types;
    result->functionType = type;
    result->returnType = returnType;
    return result;
}

static Stmt *ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    Expr *condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    Stmt *ifBody = statement();
    Stmt *elseBody = NULL;

    if (match(TOKEN_ELSE)) elseBody = statement();

    struct If *result = ALLOCATE_NODE(struct If, NODE_IF);
    result->thenBranch = ifBody;
    result->elseBranch = elseBody;
    result->condition = condition;
    return result;
}

static Stmt *whileStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    Expr *condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    Stmt *body = statement();
    struct While *result = ALLOCATE_NODE(struct While, NODE_WHILE);
    result->condition = condition;
    result->body = body;
    return result;
}

static TypeNode *typeAnnotation() {
    if (match(TOKEN_LEFT_PAREN)) {
        struct Functor *result = ALLOCATE_NODE(struct Functor, NODE_FUNCTOR);
        TypeNodeArray arguments;
        initTypeNodeArray(&arguments);

        do {
            TypeNode *type = typeAnnotation();
            writeTypeNodeArray(&arguments, type);
        } while (match(TOKEN_COMMA));

        consume(TOKEN_RIGHT_PAREN, "Expect ')' after functor type arguments.");
        consume(TOKEN_ARROW, "Expect '=>' after functor type arguments.");

        result->returnType = typeAnnotation();
        result->arguments = arguments;

        return (TypeNode *) result;
    } else {
        if (!match(TOKEN_IDENTIFIER)) {
            error("Expect identifier or functor type.");
            return NULL;
        } else {
            Token name = parser.previous; // TODO: We don't ever initialize the value arrays...
            // TODO: How is everything still working?

            if (match(TOKEN_LESS)) {
                struct Simple *target = ALLOCATE_NODE(struct Simple, NODE_SIMPLE);
                target->name = name;

                do {
                    TypeNode *argument = typeAnnotation();
                    writeTypeNodeArray(&target->generics, argument);
                } while (match(TOKEN_COMMA));

                consume(TOKEN_GREATER, "Expect '>' after generic type argument.");
                return (TypeNode *) target;
            } else {
                struct Simple *result = ALLOCATE_NODE(struct Simple, NODE_SIMPLE);
                result->name = name;
                return (TypeNode *) result;
            }
        }
    }
}

static Stmt *varDeclaration() {
    Token name = parseVariable("Expect variable name.");
    Expr *value = NULL;
    TypeNode *type = NULL;

    if (match(TOKEN_COLON)) {
        type = typeAnnotation();
    }

    if (match(TOKEN_EQUAL)) {
        value = expression();
    }

    consume(TOKEN_SEMICOLON,
            "Expect ';' after variable declaration.");

    struct Var *var = ALLOCATE_NODE(struct Var, NODE_VAR);
    var->name = name;
    var->initializer = value;
    var->type = type;
    return var;
}

static Stmt *forStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    Stmt *initializer = NULL;
    Expr *condition = NULL;
    Expr *increment = NULL;
    if (match(TOKEN_SEMICOLON)) {
        // No initializer.
    } else if (match(TOKEN_VAR)) {
        initializer = varDeclaration();
    } else {
        initializer = expressionStatement();
    }

    if (!match(TOKEN_SEMICOLON)) {
        condition = expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        increment = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
    }

    Stmt *body = statement();
    struct For *result = ALLOCATE_NODE(struct For, NODE_FOR);
    result->initializer = initializer;
    result->condition = condition;
    result->increment = increment;
    result->body = body;
    return result;
}

static Stmt *importStatement() {
    consume(TOKEN_STRING, "Expect '\"' after import.");
    Expr *s = string(false);
    match(TOKEN_SEMICOLON);
    struct Import *result = ALLOCATE_NODE(struct Import, NODE_IMPORT);
    result->expression = s;
    return result;
}

static Stmt *returnStatement() {
    if (match(TOKEN_SEMICOLON)) {
        struct Return *result = ALLOCATE_NODE(struct Return, NODE_RETURN);
        result->value = NULL;
        return (Stmt *) result;
    } else {
        Expr *value = expression();
        match(TOKEN_SEMICOLON);
        struct Return *result = ALLOCATE_NODE(struct Return, NODE_RETURN);
        result->value = value;
        return (Stmt *) result;
    }
}

static Stmt *statement() {
    Stmt* result;
    if (match(TOKEN_IF)) {
        result = ifStatement();
    } else if (match(TOKEN_RETURN)) {
        result = returnStatement();
    } else if (match(TOKEN_WHILE)) {
        result = whileStatement();
    } else if (match(TOKEN_FOR)) {
        result = forStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        result = block();
    } else if (match(TOKEN_IMPORT)) {
        result = importStatement();
    } else {
        result = expressionStatement();
    }

    while (match(TOKEN_SEMICOLON));

    return result;
}

static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;

            default:; // Do nothing.
        }

        advance();
    }
}

static Stmt *funDeclaration() {
    Token name = parseVariable("Expect function name.");
    struct Function *func = function(TYPE_FUNCTION);
    func->name = name;
    return func;
}

static Stmt *method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    Token name = parser.previous;
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
        memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    struct Function *func = function(type);
    func->name = name;
    return (Stmt *) func;
}

static Stmt *classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;

    struct Class *result = ALLOCATE_NODE(struct Class, NODE_CLASS);
    result->name = className;
    result->superclass = NULL;

    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        struct Variable *var = variable(false);

        if (identifiersEqual(&className, &parser.previous)) {
            error("A class can't inherit from itself.");
        }
        result->superclass = var;
    }

    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    StmtArray methods;
    initStmtArray(&methods);
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        writeStmtArray(&methods, method());
    }

    result->methods = methods;
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    return (Stmt *) result;
}

static Stmt *declaration() {
    if (match(TOKEN_CLASS)) {
        return classDeclaration();
    } else if (match(TOKEN_FUN)) {
        return funDeclaration();
    } else if (match(TOKEN_VAR)) {
        return varDeclaration();
    } else {
        return statement();
    }

    if (parser.panicMode) synchronize();
}

StmtArray *parseAST(const char *source) {
    initScanner(source);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    StmtArray *statements = ALLOCATE(StmtArray, 1);
    initStmtArray(statements);

    while (!match(TOKEN_EOF)) {
        writeStmtArray(statements, declaration());
    }

    consume(TOKEN_EOF, "Expect end of expression.");

    return parser.hadError ? NULL : statements;
}
