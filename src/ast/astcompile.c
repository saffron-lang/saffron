#include "astcompile.h"

#include <printf.h>
#include "astprint.h"
#include "../object.h"
#include "../debug.h"

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;


typedef struct Compiler {
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    bool hasSuperclass;
} ClassCompiler;

ClassCompiler *currentClass = NULL;
Compiler *current = NULL;

static Chunk *currentChunk() {
    return &current->function->chunk;
}

bool panicMode = false;
bool hadError = false;

static Token syntheticToken(const char *text) {
    Token token;
    token.start = text;
    token.length = (int) strlen(text);
    return token;
}

static void errorAt(Token *token, const char *message) {
    if (panicMode) return;
    panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    hadError = true;
}

static void error(const char *message) {
    Token token = syntheticToken("Fake error location");
    errorAt(&token, message); // TODO: Don't do this
}


static void emitByte(uint8_t byte) { // TODO: Real lineno
    writeChunk(currentChunk(), byte, 1);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0);
    } else {
        emitByte(OP_NIL);
    }

    emitByte(OP_RETURN);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}


static ObjFunction *endCompiler() {
    emitReturn();
    ObjFunction *function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!hadError) {
        disassembleChunk(currentChunk(), function->name != NULL
                                         ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}


static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t) constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler *compiler, FunctionType type, Token *name) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->function = newFunction();
    compiler->scopeDepth = 0;
    current = compiler;
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(name->start,
                                             name->length);
    }

    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index,
                      bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token *name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t) local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t) upvalue, false);
    }


    return -1;
}


static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local *local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void declareVariable(Token *name) {
    if (current->scopeDepth == 0) return;

    for (int i = current->localCount - 1; i >= 0; i--) {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth =
            current->scopeDepth;
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

uint8_t identifierConstant(Token *name) {
    return makeConstant(OBJ_VAL(copyString(name->start,
                                           name->length)));
}

static void getVariable(Token name) {
    uint8_t getOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
    }

    emitBytes(getOp, (uint8_t) arg);
}

static void setVariable(Token name, bool canAssign) {
    uint8_t setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        setOp = OP_SET_GLOBAL;
    }

    emitBytes(setOp, (uint8_t) arg);
}

static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;

    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth >
           current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

void compileNode(Node *node);

void compileTree(StmtArray *statements) {
    if (statements == NULL) {
        return;
    }

    for (int i = 0; i < statements->count; i++) {
        compileNode((Node *) statements->stmts[i]);
    }
}


ObjFunction *compile(StmtArray *body) {
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT, NULL);

    hadError = false;
    panicMode = false;

    compileTree(body);

    ObjFunction *function = endCompiler();
    return hadError ? NULL : function;
}

static void printToken(Token token) {
    for (int i = 0; i < token.length; i++) {
        printf("%c", token.start[i]);
    }
}

static void compileExprArray(ExprArray exprArray) {
    for (int i = 0; i < exprArray.count; i++) {
        compileNode((Node *) exprArray.exprs[i]);
    }
}

void compileNode(Node *node) {
    switch (node->type) {
        case NODE_LOGICAL:
        case NODE_BINARY: {
            struct Binary *casted = (struct Binary *) node;
            TokenType operatorType = casted->operator.type;
            compileNode((Node *) casted->left);
            compileNode((Node *) casted->right);

            switch (operatorType) {
                case TOKEN_PLUS:
                    emitByte(OP_ADD);
                    break;
                case TOKEN_MINUS:
                    emitByte(OP_SUBTRACT);
                    break;
                case TOKEN_STAR:
                    emitByte(OP_MULTIPLY);
                    break;
                case TOKEN_SLASH:
                    emitByte(OP_DIVIDE);
                    break;
                case TOKEN_BANG_EQUAL:
                    emitBytes(OP_EQUAL, OP_NOT);
                    break;
                case TOKEN_EQUAL_EQUAL:
                    emitByte(OP_EQUAL);
                    break;
                case TOKEN_GREATER:
                    emitByte(OP_GREATER);
                    break;
                case TOKEN_GREATER_EQUAL:
                    emitBytes(OP_LESS, OP_NOT);
                    break;
                case TOKEN_LESS:
                    emitByte(OP_LESS);
                    break;
                case TOKEN_LESS_EQUAL:
                    emitBytes(OP_GREATER, OP_NOT);
                    break;
            }
            break;
        }
        case NODE_GROUPING: {
            struct Grouping *casted = (struct Grouping *) node;
            compileNode((Node *) casted->expression);
            break;
        }
        case NODE_LITERAL: {
            struct Literal *casted = (struct Literal *) node;
            switch (casted->value.type) {
                case VAL_BOOL:
                    emitByte(casted->value.as.boolean ? OP_TRUE : OP_FALSE);
                    break;
                case VAL_NIL:
                    emitByte(OP_NIL);
                    break;
                default:
                    emitConstant(casted->value);
                    break;
            }
            break;
        }
        case NODE_UNARY: {
            struct Unary *casted = (struct Unary *) node;
            TokenType operatorType = casted->operator.type;
            compileNode((Node *) casted->right);

            // Emit the operator instruction.
            switch (operatorType) {
                case TOKEN_BANG:
                    emitByte(OP_NOT);
                    break;
                case TOKEN_MINUS:
                    emitByte(OP_NEGATE);
                    break;
                default:
                    return; // Unreachable.
            }
            break;
        }
        case NODE_VARIABLE: {
            struct Variable *casted = (struct Variable *) node;
            getVariable(casted->name);
            break;
        }
        case NODE_ASSIGN: {
            struct Assign *casted = (struct Assign *) node;
            compileNode((Node *) casted->value);
            setVariable(casted->name, true);
            break;
        }
        case NODE_CALL: {
            struct Call *casted = (struct Call *) node;
            if (casted->callee->self.type == NODE_GET) {
                struct Get *callee = (struct Get *) casted->callee;
                compileNode((Node *) callee->object);
                uint8_t name = identifierConstant(&callee->name);
                compileExprArray(casted->arguments);
                emitBytes(OP_INVOKE, name);
                emitByte(casted->arguments.count);
            } else if (casted->callee->self.type == NODE_SUPER) {
                struct Super *callee = (struct Super *) casted->callee;
                getVariable(syntheticToken("this"));
                uint8_t name = identifierConstant(&callee->method);
                compileExprArray(casted->arguments);
                getVariable(syntheticToken("super"));
                emitBytes(OP_SUPER_INVOKE, name);
                emitByte(casted->arguments.count);
            } else {
                compileNode((Node *) casted->callee);
                compileExprArray(casted->arguments);
                emitBytes(OP_CALL, casted->arguments.count);
            }
            break;
        }
        case NODE_GETITEM: {
            struct GetItem *casted = (struct GetItem *) node;
            compileNode(casted->object);
            compileNode(casted->index);
            emitByte(OP_GETITEM);
            break;
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            compileNode((Node *) casted->object);
            uint8_t name = identifierConstant(&casted->name);
            emitBytes(OP_GET_PROPERTY, name);
            break;
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            compileNode((Node *) casted->object);
            compileNode((Node *) casted->value);
            uint8_t name = identifierConstant(&casted->name);
            emitBytes(OP_SET_PROPERTY, name);
            break;
        }
        case NODE_SUPER: {
            struct Super *casted = (struct Super *) node;
            if (currentClass == NULL) {
                errorAt(&casted->keyword, "Can't use 'super' outside of a class.");
            } else if (!currentClass->hasSuperclass) {
                errorAt(&casted->keyword, "Can't use 'super' in a class with no superclass.");
            }

            uint8_t name = identifierConstant(&casted->method);
            getVariable(syntheticToken("this"));
            getVariable(syntheticToken("super"));
            emitBytes(OP_GET_SUPER, name);
            break;
        }
        case NODE_THIS: {
            struct This *casted = (struct This *) node;
            if (currentClass == NULL) {
                errorAt(&casted->keyword, "Can't use 'this' outside of a class.");
                return;
            }

            getVariable(casted->keyword);
            break;
        }
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            if (casted->expression) {
                compileNode((Node *) casted->expression);
            } else {
                emitByte(OP_NIL);
            }
            emitByte(OP_YIELD);
            break;
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;
            Compiler compiler;
            Token name = syntheticToken("<anon function>");
            initCompiler(&compiler, TYPE_FUNCTION, &name);
            beginScope();

            for (int i = 0; i < casted->params.count; i++) {
                declareVariable(&casted->params.tokens[i]);
                int constant = identifierConstant(&casted->params.tokens[i]);
                defineVariable(constant);
            }

            compileTree(&casted->body);

            ObjFunction *function = endCompiler();
            function->arity = casted->params.count;
            emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

            for (int i = 0; i < function->upvalueCount; i++) {
                emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
                emitByte(compiler.upvalues[i].index);
            }
            break;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;
            compileExprArray(casted->items);
            emitBytes(OP_LIST, casted->items.count);
            break;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            compileNode((Node *) casted->expression);
            emitByte(OP_POP);
            break;
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;

            declareVariable(&casted->name);
            uint8_t nameConstant = identifierConstant(&casted->name);

            if (casted->initializer) {
                compileNode((Node *) casted->initializer);
            } else {
                emitByte(OP_NIL);
            }
            defineVariable(nameConstant);


            break;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            beginScope();
            compileTree(&casted->statements);
            endScope();
            break;
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;
            Compiler compiler;
            initCompiler(&compiler, casted->functionType, &casted->name);
            beginScope();

            for (int i = 0; i < casted->params.count; i++) {
                declareVariable(&casted->params.tokens[i]);
                uint8_t constant = identifierConstant(&casted->params.tokens[i]);
                defineVariable(constant);
            }

            compileTree(&casted->body);

            ObjFunction *function = endCompiler();
            function->arity = casted->params.count;

            emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
            if (casted->functionType == TYPE_FUNCTION) {
                uint8_t global = identifierConstant(&casted->name);
                defineVariable(global);
            }

            for (int i = 0; i < function->upvalueCount; i++) {
                emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
                emitByte(compiler.upvalues[i].index);
            }
            break;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;
            Token className = casted->name;
            uint8_t nameConstant = identifierConstant(&casted->name);
            declareVariable(&casted->name);
            emitBytes(OP_CLASS, nameConstant);
            defineVariable(nameConstant);

            ClassCompiler classCompiler;
            classCompiler.enclosing = currentClass;
            classCompiler.hasSuperclass = false;
            currentClass = &classCompiler;

            if (casted->superclass != NULL) {
                compileNode((Node *) casted->superclass);

                beginScope();
                addLocal(syntheticToken("super"));
                defineVariable(0);

                getVariable(className);
                emitByte(OP_INHERIT);
                classCompiler.hasSuperclass = true;
            }

            getVariable(className);


            for (int i = 0; i < casted->methods.count; i++) {
                uint8_t constant = identifierConstant(&((struct Function *) casted->methods.stmts[i])->name);
                compileNode((Node *) casted->methods.stmts[i]);
                emitBytes(OP_METHOD, constant);
            }

            emitByte(OP_POP);

            if (classCompiler.hasSuperclass) {
                endScope();
            }

            currentClass = currentClass->enclosing;
            break;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            compileNode((Node *) casted->condition);

            int thenJump = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);
            compileNode((Node *) casted->thenBranch);

            int elseJump = emitJump(OP_JUMP);
            patchJump(thenJump);
            emitByte(OP_POP);

            if (casted->elseBranch) {
                compileNode((Node *) casted->thenBranch);
            }
            patchJump(elseJump);
            break;
        }
        case NODE_WHILE: {
            struct While *casted = (struct While *) node;
            int loopStart = currentChunk()->count;
            compileNode((Node *) casted->condition);

            int exitJump = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);
            compileNode((Node *) casted->body);
            emitLoop(loopStart);

            patchJump(exitJump);
            emitByte(OP_POP);
            break;
        }
        case NODE_FOR: {
            struct For *casted = (struct For *) node;
            beginScope();
            if (casted->initializer) {
                compileNode((Node *) casted->initializer);
            }

            int loopStart = currentChunk()->count;
            int exitJump = -1;
            if (casted->condition) {
                compileNode((Node *) casted->condition);

                // Jump out of the loop if the condition is false.
                exitJump = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP); // Condition.
            }

            if (casted->increment) {
                int bodyJump = emitJump(OP_JUMP);
                int incrementStart = currentChunk()->count;
                compileNode((Node *) casted->increment);
                emitByte(OP_POP);

                emitLoop(loopStart);
                loopStart = incrementStart;
                patchJump(bodyJump);
            }

            compileNode((Node *) casted->body);
            emitLoop(loopStart);

            if (exitJump != -1) {
                patchJump(exitJump);
                emitByte(OP_POP); // Condition.
            }

            endScope();
            break;
        }
        case NODE_BREAK:
            // TODO
            break;
        case NODE_RETURN: {
            struct Return *casted = (struct Return *) node;
            if (current->type == TYPE_SCRIPT) {
                errorAt(&casted->keyword, "Can't return from top-level code.");
            }

            if (casted->value == NULL) {
                emitReturn();
            } else {
                if (current->type == TYPE_INITIALIZER) {
                    errorAt(&casted->keyword, "Can't return a value from an initializer.");
                }

                compileNode((Node *) casted->value);
                emitByte(OP_RETURN);
            }
            break;
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;
            compileNode((Node *) casted->expression);
            emitByte(OP_IMPORT);
            break;
        }
    }
}

void markCompilerRoots() {
    Compiler *compiler = current;
    while (compiler != NULL) {
        markObject((Obj *) compiler->function);
        compiler = compiler->enclosing;
    }
}