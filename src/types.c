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
    type->genericCount = 0;
    pop();
    return type;
}

FunctorType *newFunctorType() {
    FunctorType *type = ALLOCATE_OBJ(FunctorType, OBJ_PARSE_FUNCTOR_TYPE);
    push(OBJ_VAL(type));
    initValueArray(&type->arguments);
    type->genericCount = 0;
    type->returnType = NULL;
    pop();
    return type;
}

UnionType *newUnionType() {
    UnionType *type = ALLOCATE_OBJ(UnionType, OBJ_PARSE_UNION_TYPE);
    push(OBJ_VAL(type));
    type->left = NULL;
    type->right = NULL;
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

static TypeLocal *defineLocalAndTypeDef(TypeEnvironment *typeEnvironment, const char *name, SimpleType *type) {
    Value initTypeValue;
    tableGet(&type->methods, copyString("init", 4), &initTypeValue);
    Type *initType = (Type *) AS_OBJ(initTypeValue);
    defineTypeDef(typeEnvironment, name, (Type *) type);
    return defineLocal(typeEnvironment, name, initType);
}

SimpleType *numberType;
SimpleType *boolType;
SimpleType *nilType;
SimpleType *atomType;
SimpleType *stringType;
SimpleType *neverType;
SimpleType *anyType;
SimpleType *listTypeDef;

void initGlobalEnvironment(TypeEnvironment *typeEnvironment) {
    numberType = newSimpleType();
    defineTypeDef(typeEnvironment, "Number", (Type *) numberType);
    nilType = newSimpleType();
    defineTypeDef(typeEnvironment, "Nil", (Type *) nilType);
    boolType = newSimpleType();
    defineTypeDef(typeEnvironment, "Bool", (Type *) boolType);
    atomType = newSimpleType();
    defineTypeDef(typeEnvironment, "Atom", (Type *) atomType);
    stringType = newSimpleType();
    defineTypeDef(typeEnvironment, "String", (Type *) stringType);
    neverType = newSimpleType();
    defineTypeDef(typeEnvironment, "Never", (Type *) neverType);
    anyType = newSimpleType();
    defineTypeDef(typeEnvironment, "Never", (Type *) anyType);
    listTypeDef = createListTypeDef();
    defineLocalAndTypeDef(typeEnvironment, "List", listTypeDef);

    FunctorType *printlnType = newFunctorType();
    printlnType->returnType = (Type *) nilType;
    writeValueArray(&printlnType->arguments, OBJ_VAL(anyType));
    writeValueArray(&printlnType->arguments, OBJ_VAL(anyType));
    defineLocal(typeEnvironment, "println", (Type *) printlnType);
    defineLocal(typeEnvironment, "print", (Type *) printlnType);
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

static TypeLocal *resolveLocal(struct TypeEnvironment *typeEnvironment, Token *name) {
    for (int i = typeEnvironment->localCount - 1; i >= 0; i--) {
        TypeLocal *local = &typeEnvironment->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return local;
        }
    }

    if (typeEnvironment->enclosing != NULL) {
        return resolveLocal(typeEnvironment->enclosing, name);
    }

    return NULL;
}

static TypeLocal *resolveTypeDef(struct TypeEnvironment *typeEnvironment, Token *name) {
    for (int i = typeEnvironment->typeDefCount - 1; i >= 0; i--) {
        TypeLocal *local = &typeEnvironment->typeDefs[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return local;
        }
    }

    if (typeEnvironment->enclosing != NULL) {
        return resolveTypeDef(typeEnvironment->enclosing, name);
    }

    return NULL;
}

// Get types from vm.types
// Types will include methods
// Add attributes to types
// Builtin types will also get added to vm.types
static Type *getVariableType(Token name) {
    TypeLocal *arg = resolveLocal(currentEnv, &name);
    if (arg) {
        return arg->type;
    } else {
        errorAt(&name, "Undefined variable");
        return NULL;
    }
}

static Type *getTypeDef(Token name) {
    TypeEnvironment *tenv = currentEnv;
    TypeLocal *arg = resolveTypeDef(currentEnv, &name);
    if (arg) {
        return arg->type;
    } else {
        errorAt(&name, "Undefined type");
        return NULL;
    }
}

static bool isSubType(Type *subclass, Type *superclass) {
    // TODO: Make this actually work
    // TODO: Maybe this should actually be "isSubClass", left to right
    // If left is a subclass of right, then we can assign right to left
    // Subclasses include generics

    if (subclass == superclass) {
        return true;
    }

    if (superclass == neverType) {
        return false;
    }

    if (superclass == anyType) {
        return true;
    }

    switch (superclass->obj.type) {
        case (OBJ_PARSE_TYPE): {
            if (subclass->obj.type != OBJ_PARSE_TYPE) {
                return false;
            }

            SimpleType *subclassType = (SimpleType *) subclass;
            if (!subclassType->superType) {
                return false;
            } else {
                return isSubType(subclassType->superType, superclass);
            }
        }
        case (OBJ_PARSE_FUNCTOR_TYPE): {
            FunctorType *superclassType = (FunctorType *) superclass;
            if (subclass->obj.type != OBJ_PARSE_FUNCTOR_TYPE) {
                return false;
            }

            FunctorType *subclassType = (FunctorType *) subclass;

            if (superclassType->arguments.count != subclassType->arguments.count) {
                return false;
            }

            for (int i = 0; i < superclassType->arguments.count; i++) {
                Type *superArgType = AS_OBJ(superclassType->arguments.values[i]);
                Type *subArgType = AS_OBJ(subclassType->arguments.values[i]);
                if (!isSubType(subArgType, superArgType)) {
                    return false;
                }
            }

            return isSubType(subclassType->returnType, superclassType->returnType);
        }
        case (OBJ_PARSE_GENERIC_TYPE): {

            break;
        }
        case (OBJ_PARSE_UNION_TYPE): {
            UnionType *superclassType = (UnionType *) superclass;
            return isSubType(subclass, superclassType->left)
                   || isSubType(subclass, superclassType->right);
        }
        case (OBJ_PARSE_INTERFACE_TYPE): {

            break;
        }
    }

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
            return boolType;
        case VAL_NIL:
            return nilType;
        case VAL_NUMBER:
            return numberType;
        case VAL_OBJ: {
            Obj *obj = AS_OBJ(value);
            switch (obj->type) {
                case OBJ_STRING: {
                    return stringType;
                }
                case OBJ_ATOM: {
                    return atomType;
                }
            }
        }
    }

#endif

    return NULL;
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

Type *currentClassType = NULL;
FunctorType *currentFuncType = NULL;

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

            if (!isSubType(namedType, valueType)) {
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
            Type *calleeType = evaluateNode((Node *) casted->callee);

            if (calleeType->obj.type != OBJ_PARSE_FUNCTOR_TYPE) {
                errorAt(&casted->paren, "Type is not callable");
                return(NULL);
            }

            FunctorType *calleeFunctor = calleeType;

            if (casted->arguments.count > calleeFunctor->arguments.count) {
//                errorAt(&casted->paren, "Too many arguments provided");
//                return(NULL);
            }

            for (int i = 0; i < casted->arguments.count; i++) {
                Type *argType = evaluateNode((Node *) casted->arguments.exprs[i]);
                if (!isSubType(argType, AS_OBJ(calleeFunctor->arguments.values[i]))) {
                    errorAt(&casted->paren, "Type mismatch");
                    return (NULL);
                }
            }

            return calleeFunctor->returnType;
        }
        case NODE_GETITEM: {
            struct GetItem *casted = (struct GetItem *) node;
            Type *type = evaluateNode((Node *) casted->object);
            if (!isSubType(type, listTypeDef)) {
                error("GetItem on something other than a list");
                return (NULL);
            }
            GenericType *genericType = (GenericType *) type;
            Type *indexType = evaluateNode(casted->index);
            if (!isSubType(indexType, numberType)) {
                error("Index must be a number");
                return (NULL);
            }

            if (genericType->generics.count) {
                return AS_OBJ(genericType->generics.values[0]);
            } else {
                return neverType;
            }
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
                    errorAt(&casted->name, "Invalid field");
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
                    errorAt(&casted->name, "Invalid field");
                }
            }

            if (!isSubType(valueType, AS_TYPE(fieldType))) {
                error("Type mismatch in setter");
            }

            return AS_TYPE(fieldType);
        }
        case NODE_SUPER: {
            SimpleType *currentClass = (SimpleType *) currentClassType;
            return currentClass->superType;
        }
        case NODE_THIS: {
            return currentClassType;
        }
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            evaluateNode((Node *) casted->expression);
            return anyType;
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_FUNCTION);

            FunctorType *type = newFunctorType();
            FunctorType *oldFuncType = currentFuncType;
            currentFuncType = type;
            struct Functor *functorNode = casted->self.type;
            for (int i = 0; i < casted->params.count; i++) {
                TypeNode *typeNode = functorNode->arguments.typeNodes[i];
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

            type->returnType = evaluateNode((Node *) functorNode->returnType);
            evaluateTypes(&casted->body);

            if (!type->returnType) {
                type->returnType = (Type *) nilType;
            }

            currentEnv = currentEnv->enclosing;
            currentFuncType = oldFuncType;

            return type;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;

            Type *itemType = neverType;
            if (casted->items.count > 0) {
                if (casted->items.count > 1) {
                    evaluateExprTypes(&casted->items);
                }

                itemType = evaluateNode((Node *) casted->items.exprs[0]);
            }

            GenericType *type = newGenericType();
            writeValueArray(&type->generics, OBJ_VAL(itemType));
            type->target = listTypeDef;
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

            if (casted->initializer != NULL) {
                Type *valType = evaluateNode((Node *) casted->initializer);
                if (varType) {
                    if (!isSubType(valType, varType)) {
                        errorAt(&casted->name, "Type mismatch in var");
                    }
                } else {
                    varType = valType;
                }
            }

            TypeLocal typeLocal = {
                    casted->name,
                    0,
                    false,
                    varType,
            };

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

            Type* oldFuncType = currentFuncType;
            FunctorType *type = newFunctorType();
            currentFuncType = type;
            for (int i = 0; i < casted->params.count; i++) {
                TypeNode *typeNode = casted->paramTypes.typeNodes[i];
                Type *argType;
                if (typeNode != NULL) {
                    argType = evaluateNode((Node *) typeNode);
                } else {
                    argType = (Type *) anyType;
                }

                writeValueArray(&type->arguments, OBJ_VAL(argType));

                TypeLocal typeLocal = {
                        casted->params.tokens[i],
                        0,
                        false,
                        (Type *) argType,
                };

                currentEnv->locals[currentEnv->localCount] = typeLocal;
                currentEnv->localCount++;

            }

            type->returnType = evaluateNode((Node *) casted->returnType);
            evaluateTypes(&casted->body);
            if (!type->returnType) {
                type->returnType = (Type *) nilType;
            }

            currentEnv = currentEnv->enclosing;

            TypeLocal typeLocal = {
                    casted->name,
                    0,
                    false,
                    (Type *) type,
            };

            currentEnv->locals[currentEnv->localCount] = typeLocal;
            currentEnv->localCount++;

            currentFuncType = oldFuncType;
            return (Type *) type;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;

            SimpleType *classType = newSimpleType();
            Type *oldClass = currentClassType;
            currentClassType = (Type *) classType;
            FunctorType *classFunctionType = newFunctorType();
            for (int j = 0; j < casted->body.count; j++) {

                if (casted->body.stmts[j]->self.type == NODE_FUNCTION) {
                    struct Function *method = (struct Function *) casted->body.stmts[j];
                    TypeEnvironment typeEnv;
                    initTypeEnvironment(&typeEnv, method->functionType);

                    TypeLocal typeLocal = {
                            syntheticToken("this"),
                            0,
                            false,
                            (Type *) classType,
                    };

                    typeEnv.locals[currentEnv->localCount] = typeLocal;
                    typeEnv.localCount++;

                    FunctorType *type = newFunctorType();
                    FunctorType *oldFuncType = currentFuncType;
                    currentFuncType = type;
                    for (int i = 0; i < method->paramTypes.count; i++) {
                        TypeNode *typeNode = method->paramTypes.typeNodes[i];
                        Type *argType;
                        if (typeNode != NULL) {
                            argType = evaluateNode((Node *) typeNode);
                        } else {
                            argType = (Type *) anyType;
                        }

                        writeValueArray(&type->arguments, OBJ_VAL(argType));

                        TypeLocal typeLocal = {
                                method->params.tokens[i],
                                0,
                                false,
                                (Type *) argType,
                        };

                        typeEnv.locals[currentEnv->localCount] = typeLocal;
                        typeEnv.localCount++;

                    }

                    tableSet(
                            &classType->methods,
                            copyString(method->name.start, method->name.length),
                            OBJ_VAL(type)
                    );

                    if (method->functionType != TYPE_INITIALIZER) {
                        type->returnType = evaluateNode((Node *) method->returnType);
                    } else {
                        type->returnType = (Type *) classType;
                        classFunctionType->arguments = type->arguments;
                    }

                    evaluateTypes(&method->body);
                    if (!type->returnType) {
                        type->returnType = (Type *) nilType;
                    }

                    currentEnv = currentEnv->enclosing;
                    currentFuncType = oldFuncType;
                } else {
                    struct Var *var = (struct Var *) casted->body.stmts[j];
                    Type *type = evaluateNode((Node *) var->type);
                    if (var->initializer) {
                        if (!isSubType(type, evaluateNode((Node *) var->initializer))) {
                            errorAt(&var->name, "Type mismatch.");
                        }
                    }
                    tableSet(
                            &classType->fields,
                            copyString(var->name.start, var->name.length),
                            OBJ_VAL(type)
                    );
                }
            }

            classType->superType = NULL;

            if (casted->superclass) {
                classType->superType = getTypeDef(casted->superclass->name);
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

            currentClassType = oldClass;
            return (Type *) classType;
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
            Type* value = evaluateNode((Node *) casted->value);
            if (currentFuncType->returnType) {
                if (!isSubType(value, currentFuncType->returnType)) {
                    errorAt(&casted->keyword, "Return type mismatch");
                }
            } else {
                currentFuncType->returnType = value;
            }
            FunctorType* current = currentFuncType;
            return value;
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
            FunctorType *type = newFunctorType();

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
        case NODE_UNION: {
            struct Union *casted = (struct Union *) node;
            UnionType *type = newUnionType();
            type->left = evaluateNode((Node *) casted->left);
            type->right = evaluateNode((Node *) casted->right);
            return type;
        }
    }

    return NULL;
}

void freeType(Type *type) {
    switch (type->obj.type) {
        case OBJ_PARSE_FUNCTOR_TYPE:
            FREE(FunctorType, type);
            break;
        case OBJ_PARSE_UNION_TYPE:
            FREE(UnionType, type);
            break;
        case OBJ_PARSE_INTERFACE_TYPE:
            FREE(InterfaceType, type);
            break;
        case OBJ_PARSE_TYPE:
            FREE(SimpleType, type);
            break;
        case OBJ_PARSE_GENERIC_TYPE:
            FREE(GenericType, type);
            break;
    }
}

void markType(Type *type) {
    switch (type->obj.type) {
        case OBJ_PARSE_FUNCTOR_TYPE: {
            FunctorType *casted = (FunctorType *) type;
            markArray(&casted->arguments);
            markObject((Obj *) casted->returnType);
            break;
        }
        case OBJ_PARSE_UNION_TYPE: {
            UnionType *casted = (UnionType *) type;
            markObject((Obj *) casted->left);
            markObject((Obj *) casted->right);
            break;
        }
        case OBJ_PARSE_INTERFACE_TYPE: {
            struct InterfaceType *casted = (InterfaceType *) type;
            markTable(&casted->fields);
            markTable(&casted->methods);
            break;
        }
        case OBJ_PARSE_TYPE: {
            struct SimpleType *casted = (SimpleType *) type;
            markObject((Obj *) casted->superType);
            markTable(&casted->fields);
            markTable(&casted->methods);
            break;
        }
        case OBJ_PARSE_GENERIC_TYPE: {
            struct GenericType *casted = (GenericType *) type;
            markObject((Obj *) casted->target);
            markArray(&casted->generics);
            break;
        }
    }
}

void markTypecheckerRoots() {
    TypeEnvironment *typeEnvironment = currentEnv;
    while (typeEnvironment != NULL) {
        for (int i = 0; i < typeEnvironment->localCount; i++) {
            markObject((Obj *) typeEnvironment->locals[i].type);
        }
        for (int i = 0; i < typeEnvironment->typeDefCount; i++) {
            markObject((Obj *) typeEnvironment->typeDefs[i].type);
        }
        typeEnvironment = typeEnvironment->enclosing;
    }
}