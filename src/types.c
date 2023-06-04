#include <printf.h>
#include "types.h"
#include "object.h"
#include "vm.h"
#include "libc/list.h"


Type *evaluateNode(Node *node);

TypeEnvironment *currentEnv = NULL;

SimpleType *newSimpleType() {
    SimpleType *type = ALLOCATE_OBJ(SimpleType, OBJ_PARSE_TYPE);
    push(OBJ_VAL(type));
    initTable(&type->methods);
    initTable(&type->fields);
    initValueArray(&type->arguments);
    type->genericCount = 0;
    pop();
    return type;
}

GenericType *newGenericType() {
    GenericType *type = ALLOCATE_OBJ(GenericType, OBJ_PARSE_GENERIC_TYPE);
    push(OBJ_VAL(type));
    type->target = NULL;
    initValueArray(&type->generics);
    pop();
    return type;
}

static bool panicMode = false;
static bool hadError = false;

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

static TypeLocal *defineTypeDef(TypeEnvironment *typeEnvironment, const char *name, Type *type) {
    TypeLocal typeLocal = {
            syntheticToken(name),
            0,
            false,
            (Type *) type,
    };

    typeEnvironment->typeDefs[currentEnv->typeDefCount] = typeLocal;
    typeEnvironment->typeDefCount++;

    return &typeEnvironment->typeDefs[typeEnvironment->typeDefCount - 1];
}

static TypeLocal *defineLocal(TypeEnvironment *typeEnvironment, const char *name, Type *type) {
    TypeLocal typeLocal = {
            syntheticToken(name),
            0,
            false,
            (Type *) type,
    };

    typeEnvironment->locals[currentEnv->localCount] = typeLocal;
    typeEnvironment->localCount++;

    return &typeEnvironment->locals[typeEnvironment->localCount - 1];
}

SimpleType* numberType;
SimpleType* boolType;
SimpleType* nilType;
SimpleType* atomType;
SimpleType* stringType;
SimpleType* neverType;

void initGlobalEnvironment(TypeEnvironment *typeEnvironment) {
    numberType = newSimpleType();
    defineTypeDef(typeEnvironment, "number", (Type *) numberType);
    nilType = newSimpleType();
    defineTypeDef(typeEnvironment, "nil", (Type *) nilType);
    boolType = newSimpleType();
    defineTypeDef(typeEnvironment, "bool", (Type *) boolType);
    atomType = newSimpleType();
    defineTypeDef(typeEnvironment, "atom", (Type *) atomType);
    stringType = newSimpleType();
    defineTypeDef(typeEnvironment, "string", (Type *) stringType);
    neverType = newSimpleType();
    defineTypeDef(typeEnvironment, "never", (Type *) neverType);

    defineTypeDef(typeEnvironment, "list", listTypeDef());

    SimpleType* printlnType = newSimpleType();
    printlnType->returnType = (Type *) nilType;
    writeValueArray(&printlnType->arguments, OBJ_VAL(nilType));
    defineLocal(typeEnvironment, "println", (Type *) printlnType);
}

void initTypeEnvironment(TypeEnvironment *typeEnvironment, FunctionType type) {
    typeEnvironment->enclosing = currentEnv;
    typeEnvironment->type = type;
    typeEnvironment->localCount = 0;
    typeEnvironment->typeDefCount = 0;
    typeEnvironment->scopeDepth = 0;
    currentEnv = typeEnvironment;

    TypeLocal *local = &currentEnv->locals[currentEnv->localCount++];
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

struct Functor *initFunctor(TypeNodeArray types, TypeNode *returnType) {
    struct Functor *type = ALLOCATE_NODE(struct Functor, NODE_FUNCTOR);
    type->arguments = types;
    type->returnType = returnType;
    return type;
}

struct Simple *initSimple(Token name) {
    struct Simple *type = ALLOCATE_NODE(struct Simple, NODE_FUNCTOR);
    type->name = name;
    return type;
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(struct TypeEnvironment *typeEnvironment, Token *name) {
    for (int i = typeEnvironment->localCount - 1; i >= 0; i--) {
        TypeLocal *local = &typeEnvironment->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    if (currentEnv->enclosing != NULL) {
        return resolveLocal(currentEnv->enclosing, name);
    }

    return -1;
}

static int resolveTypeDef(struct TypeEnvironment *typeEnvironment, Token *name) {
    for (int i = typeEnvironment->typeDefCount - 1; i >= 0; i--) {
        TypeLocal *local = &typeEnvironment->typeDefs[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    if (currentEnv->enclosing != NULL) {
        return resolveTypeDef(currentEnv->enclosing, name);
    }

    return -1;
}


// Get types from vm.types
// Types will include methods
// Add attributes to types
// Builtin types will also get added to vm.types
static Type *getVariableType(Token name) {
    int arg = resolveLocal(currentEnv, &name);
    if (arg != -1) {
        return currentEnv->locals[arg].type;
    } else {
        error("Undefined variable");
        return NULL;
    }
}

static Type *getTypeDef(Token name) {
    TypeEnvironment *tenv = currentEnv;
    int arg = resolveTypeDef(currentEnv, &name);
    if (arg != -1) {
        return currentEnv->typeDefs[arg].type;
    } else {
        error("Undefined type");
        return NULL;
    }
}

static bool typesEqual(Type *left, Type *right) {
    // TODO: Make this actually work
    // TODO: Maybe this should actually be "isSubClass", left to right
    // If right is subclass of left, then we can assign right to left
    // Subclasses include generics
    return true;
}


Type *getTypeOf(Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
#else
    switch (value.type) {
        case VAL_BOOL:
            return getTypeDef(syntheticToken("bool"));
        case VAL_NIL:
            return getTypeDef(syntheticToken("nil"));
        case VAL_NUMBER:
            return getTypeDef(syntheticToken("number"));
        case VAL_OBJ: {
            Obj *obj = AS_OBJ(value);
            switch (obj->type) {
                case OBJ_STRING: {
                    return getTypeDef(syntheticToken("atom"));
                }
                case OBJ_ATOM: {
                    return getTypeDef(syntheticToken("string"));
                }
            }
        }
    }

#endif

    Token token;
    return (TypeNode *) getTypeDef(token);
}

void evaluateTypes(StmtArray *statements) {
    for (int i = 0; i < statements->count; i++) {
        evaluateNode((Node *) statements->stmts[i]);
    }
}

void evaluateTree(StmtArray *statements) {
    TypeEnvironment typeEnv;
    initTypeEnvironment(&typeEnv, TYPE_SCRIPT);
    initGlobalEnvironment(&typeEnv);
    currentEnv = &typeEnv;
    evaluateTypes(statements);
    currentEnv = typeEnv.enclosing;
}

void evaluateExprTypes(ExprArray *exprs) {
    for (int i = 0; i < exprs->count; i++) {
        evaluateNode((Node *) exprs->exprs[i]);
    }
}

Type *evaluateNode(Node *node) {
    if (node == NULL) {
        return NULL;
    }
    switch (node->type) {
        case NODE_BINARY: {
            struct Binary *casted = (struct Binary *) node;
            evaluateNode((Node *) casted->right);

            return evaluateNode((Node *) casted->left);
        }
        case NODE_GROUPING: {
            struct Grouping *casted = (struct Grouping *) node;
            evaluateNode((Node *) casted->expression);
            casted->self.type = casted->expression->type;
            break;
        }
        case NODE_LITERAL: {
            struct Literal *casted = (struct Literal *) node;
            return getTypeOf(casted->value);
        }
        case NODE_UNARY: {
            struct Unary *casted = (struct Unary *) node;
            Type *right = evaluateNode((Node *) casted->right);

            switch (casted->operator.type) {
                case TOKEN_BANG:
                    return getTypeOf(BOOL_VAL(true));
                case TOKEN_MINUS:
                    return right;
                default:
                    return NULL; // Unreachable.
            }
        }
        case NODE_VARIABLE: {
            struct Variable *casted = (struct Variable *) node;
            return getVariableType(casted->name);
        }
        case NODE_ASSIGN: {
            struct Assign *casted = (struct Assign *) node;
            Type *valueType = evaluateNode((Node *) casted->value);
            Type *namedType = getVariableType(casted->name);

            if (!typesEqual(namedType, valueType)) {
                errorAt(&casted->name, "Type mismatch");
            }

            return namedType ? namedType : valueType;
        }
        case NODE_LOGICAL: {
            struct Logical *casted = (struct Logical *) node;
            evaluateNode((Node *) casted->left);
            evaluateNode((Node *) casted->right);
            return getTypeOf(BOOL_VAL(true));
        }
        case NODE_CALL: {
            struct Call *casted = (struct Call *) node;
            SimpleType *calleeType = evaluateNode((Node *) casted->callee);

            if (casted->arguments.count > calleeType->arguments.count) {
                error("Too many arguments provided");
            }

            for (int i = 0; i < casted->arguments.count; i++) {
                Type *argType = evaluateNode((Node *) casted->arguments.exprs[i]);
                if (!typesEqual(argType, AS_OBJ(calleeType->arguments.values[i]))) {
                    error("Type mismatch");
                }
            }

            return calleeType->returnType;
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            Type *objectType = evaluateNode((Node *) casted->object);
            SimpleType *rootType = (SimpleType *) objectType;

            if (objectType->obj.type == OBJ_PARSE_GENERIC_TYPE) {
                rootType = (SimpleType *) ((GenericType *) objectType)->target;
            }

            Value fieldType;

            if (!tableGet(&rootType->methods, copyString(casted->name.start, casted->name.length), &fieldType)) {
                if (!tableGet(&rootType->fields, copyString(casted->name.start, casted->name.length), &fieldType)) {
                    error("Invalid field");
                }
            }

            return AS_TYPE(fieldType);
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            Type *valueType = evaluateNode((Node *) casted->value);

            Type *objectType = evaluateNode((Node *) casted->object);
            SimpleType *rootType = (SimpleType *) objectType;

            if (objectType->obj.type == OBJ_PARSE_GENERIC_TYPE) {
                rootType = (SimpleType *) ((GenericType *) objectType)->target;
            }

            Value fieldType;

            if (!tableGet(&rootType->methods, copyString(casted->name.start, casted->name.length), &fieldType)) {
                if (!tableGet(&rootType->fields, copyString(casted->name.start, casted->name.length), &fieldType)) {
                    error("Invalid field");
                }
            }

            if (!typesEqual(valueType, AS_TYPE(fieldType))) {
                error("Type mismatch in setter");
            }

            return AS_TYPE(fieldType);
        }
        case NODE_SUPER: {
            break; // TODO
        }
        case NODE_THIS: {
            break; // TODO
        }
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            break; // TODO
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_FUNCTION);

            SimpleType *type = newSimpleType();
            for (int i = 0; i < casted->params.count; i++) {
                TypeNode *typeNode = casted->paramTypes.typeNodes[i];
                if (typeNode != NULL) {
                    Type *argType = evaluateNode((Node *) typeNode);
                    writeValueArray(&type->arguments, OBJ_VAL(argType));

                    TypeLocal typeLocal = {
                            casted->params.tokens[i],
                            0,
                            false,
                            (Type *) argType,
                    };

                    currentEnv->locals[currentEnv->localCount] = typeLocal;
                    currentEnv->localCount++;
                } else {
                    writeValueArray(&type->arguments, NIL_VAL);
                }
            }

            type->returnType = evaluateNode((Node *) casted->returnType);
            evaluateTypes(&casted->body);

            currentEnv = currentEnv->enclosing;

            return type;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;

            Type *itemType = getTypeOf(NIL_VAL); // TODO: Never
            if (casted->items.count > 0) {
                if (casted->items.count > 1) {
                    evaluateExprTypes(&casted->items);
                }

                itemType = evaluateNode((Node *) casted->items.exprs[0]);
            }

            GenericType *type = newGenericType();
            writeValueArray(&type->generics, OBJ_VAL(itemType));
            type->target = getTypeDef(syntheticToken("list"));
            return (Type *) type;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            evaluateNode((Node *) casted->expression);
            break;
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;
            Type *varType = evaluateNode((Node *) casted->type);

            TypeLocal typeLocal = {
                    casted->name,
                    0,
                    false,
                    varType,
            };

            if (casted->initializer != NULL) {
                Type *valType = evaluateNode((Node *) casted->initializer);
                if (!typesEqual(valType, varType)) {
                    errorAt(&casted->name, "Type mismatch in var");
                }
            }

            if (currentEnv->localCount > UINT8_COUNT) {
                errorAt(&casted->name, "Too many locals");
            }

            currentEnv->locals[currentEnv->localCount] = typeLocal;
            currentEnv->localCount++;
            return NULL;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            evaluateTypes(&casted->statements);
            return NULL;
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, casted->functionType);

            SimpleType *type = newSimpleType();
            for (int i = 0; i < casted->params.count; i++) {
                TypeNode *typeNode = casted->paramTypes.typeNodes[i];
                if (typeNode != NULL) {
                    Type *argType = evaluateNode((Node *) typeNode);
                    writeValueArray(&type->arguments, OBJ_VAL(argType));

                    TypeLocal typeLocal = {
                            casted->params.tokens[i],
                            0,
                            false,
                            (Type *) argType,
                    };

                    currentEnv->locals[currentEnv->localCount] = typeLocal;
                    currentEnv->localCount++;
                } else {
                    writeValueArray(&type->arguments, NIL_VAL);
                }
            }

            type->returnType = evaluateNode((Node *) casted->returnType);
            evaluateTypes(&casted->body);

            currentEnv = currentEnv->enclosing;

            TypeLocal typeLocal = {
                    casted->name,
                    0,
                    false,
                    (Type *) type,
            };

            currentEnv->locals[currentEnv->localCount] = typeLocal;
            currentEnv->localCount++;

            return (Type *) type;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;

            SimpleType *classType = newSimpleType();
            SimpleType *classFunctionType = newSimpleType();
            for (int j = 0; j < casted->methods.count; j++) {
                struct Function *method = (struct Function *) casted->methods.stmts[j];
                TypeEnvironment typeEnv;
                initTypeEnvironment(&typeEnv, method->functionType);

                SimpleType *type = newSimpleType();
                for (int i = 0; i < method->paramTypes.count; i++) {
                    TypeNode *typeNode = method->paramTypes.typeNodes[i];
                    if (typeNode != NULL) {
                        Type *argType = evaluateNode((Node *) typeNode);
                        writeValueArray(&type->arguments, OBJ_VAL(argType));

                        TypeLocal typeLocal = {
                                method->params.tokens[i],
                                0,
                                false,
                                (Type *) argType,
                        };

                        currentEnv->locals[currentEnv->localCount] = typeLocal;
                        currentEnv->localCount++;
                    } else {
                        writeValueArray(&type->arguments, NIL_VAL);
                    }

                    tableSet(
                            &classType->methods,
                            copyString(method->name.start, method->name.length),
                            OBJ_VAL(type)
                    );

                    if (method->functionType == TYPE_INITIALIZER) {
                        classFunctionType->arguments = type->arguments;
                    }
                }

                if (method->functionType != TYPE_INITIALIZER) {
                    method->returnType = (TypeNode *) evaluateNode((Node *) method->returnType);
                } else {
                    method->returnType = (TypeNode *) classType;
                }

                evaluateTypes(&method->body);

                currentEnv = currentEnv->enclosing;
            }

            classFunctionType->returnType = (Type *) classType;

            TypeLocal typeFuncLocal = {
                    casted->name,
                    0,
                    false,
                    (Type *) classFunctionType,
            };

            currentEnv->locals[currentEnv->localCount] = typeFuncLocal;
            currentEnv->localCount++;


            TypeLocal typeDefLocal = {
                    casted->name,
                    0,
                    false,
                    (Type *) classFunctionType,
            };

            currentEnv->typeDefs[currentEnv->typeDefCount] = typeDefLocal;
            currentEnv->typeDefCount++;

            break;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            evaluateNode((Node *) casted->condition);
            evaluateNode((Node *) casted->thenBranch);
            evaluateNode((Node *) casted->elseBranch);
            return NULL;
        }
        case NODE_WHILE: {
            struct While *casted = (struct While *) node;
            evaluateNode((Node *) casted->condition);
            evaluateNode((Node *) casted->body);
            return NULL;
        }
        case NODE_FOR: {
            struct For *casted = (struct For *) node;
            evaluateNode((Node *) casted->initializer);
            evaluateNode((Node *) casted->condition);
            evaluateNode((Node *) casted->increment);
            evaluateNode((Node *) casted->body);
            return NULL;
        }
        case NODE_BREAK: {
            return NULL;
        }
        case NODE_RETURN: {
            struct Return *casted = (struct Return *) node;
            return evaluateNode((Node *) casted->value);
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;

//            TypeEnvironment typeEnv;
//            initTypeEnvironment(&typeEnv, TYPE_FUNCTION);
//
//            SimpleType *type = newSimpleType();
//            for (int i = 0; i < casted->params.count; i++) {
//                TypeNode *typeNode = casted->paramTypes.typeNodes[i];
//                if (typeNode != NULL) {
//                    Type *argType = evaluateNode((Node *) typeNode);
//                    writeValueArray(&type->arguments, OBJ_VAL(argType));
//
//                    TypeLocal typeLocal = {
//                            casted->params.tokens[i],
//                            0,
//                            false,
//                            (Type *) argType,
//                    };
//
//                    currentEnv->locals[currentEnv->localCount] = typeLocal;
//                    currentEnv->localCount++;
//                } else {
//                    writeValueArray(&type->arguments, NIL_VAL);
//                }
//            }
//
//            type->returnType = evaluateNode((Node *) casted->returnType);
//            evaluateTypes(&casted->body);
//
//            currentEnv = currentEnv->enclosing;
//
//            TypeLocal typeLocal = {
//                    casted->name,
//                    0,
//                    false,
//                    (Type *) type,
//            };
//
//            currentEnv->locals[currentEnv->localCount] = typeLocal;
//            currentEnv->localCount++;
//
//            return (Type *) type;

            return NULL;
        }
        case NODE_FUNCTOR: {
            struct Functor *casted = (struct Functor *) node;
            SimpleType *type = newSimpleType();

            for (int i = 0; i < casted->arguments.count; i++) {
                TypeNode *typeNode = casted->arguments.typeNodes[i];
                if (typeNode != NULL) {
                    Type *argType = evaluateNode((Node *) typeNode);
                    writeValueArray(&type->arguments, OBJ_VAL(argType));
                } else {
                    writeValueArray(&type->arguments, NIL_VAL);
                }
            }

            type->returnType = evaluateNode((Node *) casted->returnType);

            return (Type *) type;
        }
        case NODE_SIMPLE: {
            struct Simple *casted = (struct Simple *) node;
            Type *type = getTypeDef(casted->name);
            return type;
        }
    }

    return NULL;
}