#include <printf.h>
#include "types.h"
#include "object.h"
#include "vm.h"
#include "libc/list.h"
#include "files.h"
#include "ast/astparse.h"
#include "libc/time.h"
#include "libc/map.h"


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

static void defineTypeDef(TypeEnvironment *typeEnvironment, const char *name, Type *type) {
    tableSet(&typeEnvironment->typeDefs, copyString(name, strlen(name)), OBJ_VAL(type));
}

static void *defineLocal(TypeEnvironment *typeEnvironment, const char *name, Type *type) {
    tableSet(&typeEnvironment->locals, copyString(name, strlen(name)), OBJ_VAL(type));
}

static void *defineLocalAndTypeDef(TypeEnvironment *typeEnvironment, const char *name, SimpleType *type) {
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
SimpleType *mapTypeDef;
FunctorType *printlnType;
FunctorType *spawnType;
SimpleType *taskTypeDef;

Table modules;

void makeTypes() {
    numberType = newSimpleType();
    nilType = newSimpleType();
    boolType = newSimpleType();
    atomType = newSimpleType();
    stringType = newSimpleType();
    neverType = newSimpleType();
    anyType = newSimpleType();
    listTypeDef = createListTypeDef();
    mapTypeDef = createMapTypeDef();

    printlnType = newFunctorType();
    printlnType->returnType = (Type *) nilType;
    writeValueArray(&printlnType->arguments, OBJ_VAL(anyType));
    writeValueArray(&printlnType->arguments, OBJ_VAL(anyType));

    // Tasks
    taskTypeDef = (SimpleType *) newSimpleType();

    FunctorType *isReady = newFunctorType();
    FunctorType *getResult = newFunctorType();

    isReady->returnType = (Type *) boolType;
    getResult->returnType = (Type *) anyType;
    tableSet(&taskTypeDef->methods, copyString("isReady", 7), OBJ_VAL(isReady));
    tableSet(&taskTypeDef->methods, copyString("getResult", 9), OBJ_VAL(getResult));

    // Spawn
    spawnType = newFunctorType();
    spawnType->returnType = (Type *) taskTypeDef;
    FunctorType *spawnArgType = newFunctorType();

    spawnArgType->returnType = (Type *) anyType;
    writeValueArray(&spawnType->arguments, OBJ_VAL(spawnArgType));

    // Time
    initTable(&modules);
    tableSet(&modules, copyString("time", 4), OBJ_VAL(createTimeModuleType()));
}

void initGlobalEnvironment(TypeEnvironment *typeEnvironment) {
    defineTypeDef(typeEnvironment, "Number", (Type *) numberType);
    defineTypeDef(typeEnvironment, "Nil", (Type *) nilType);
    defineTypeDef(typeEnvironment, "Bool", (Type *) boolType);
    defineTypeDef(typeEnvironment, "Atom", (Type *) atomType);
    defineTypeDef(typeEnvironment, "String", (Type *) stringType);
    defineTypeDef(typeEnvironment, "Never", (Type *) neverType);
    defineTypeDef(typeEnvironment, "Any", (Type *) anyType);
    defineTypeDef(typeEnvironment, "Task", (Type *) taskTypeDef);
    defineLocalAndTypeDef(typeEnvironment, "List", listTypeDef);
    defineLocalAndTypeDef(typeEnvironment, "Map", mapTypeDef);

    defineLocal(typeEnvironment, "println", (Type *) printlnType);
    defineLocal(typeEnvironment, "print", (Type *) printlnType);
    defineLocal(typeEnvironment, "spawn", (Type *) spawnType);
}

void initTypeEnvironment(TypeEnvironment *typeEnvironment, FunctionType type) {
    typeEnvironment->enclosing = currentEnv;
    typeEnvironment->type = type;
    initTable(&typeEnvironment->locals);
    initTable(&typeEnvironment->typeDefs);
    typeEnvironment->scopeDepth = 0;
    currentEnv = typeEnvironment;
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

static Type *resolveLocal(struct TypeEnvironment *typeEnvironment, Token *name) {
    Value valueType;
    if (tableGet(&typeEnvironment->locals, copyString(name->start, name->length), &valueType)) {
        return AS_OBJ(valueType);
    }

    if (typeEnvironment->enclosing != NULL) {
        return resolveLocal(typeEnvironment->enclosing, name);
    }

    return NULL;
}

static Type *resolveTypeDef(struct TypeEnvironment *typeEnvironment, Token *name) {
    Value valueType;
    if (tableGet(&typeEnvironment->typeDefs, copyString(name->start, name->length), &valueType)) {
        return AS_OBJ(valueType);
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
    Type *arg = resolveLocal(currentEnv, &name);
    if (arg) {
        return arg;
    } else {
        TypeEnvironment *tenv = currentEnv;
        errorAt(&name, "Undefined variable");
        return NULL;
    }
}

static Type *getTypeDef(Token name) {
    TypeEnvironment *tenv = currentEnv;
    Type *arg = resolveTypeDef(currentEnv, &name);
    if (arg) {
        return arg;
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

    if (subclass->obj.type == OBJ_PARSE_GENERIC_TYPE) {
        GenericType *subclassType = (GenericType *) subclass;
        if (isSubType(subclassType->target, superclass)) {
            return true;
        }
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

    return false;
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

Type *evaluateBlock(StmtArray *statements) {
    for (int i = 0; i < statements->count; i++) {
        if (i == statements->count) {
            return evaluateNode((Node *) statements->stmts[i]);
        } else {
            evaluateNode((Node *) statements->stmts[i]);
        }
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
Type *currentAssignmentType = NULL;
FunctorType *currentFuncType = NULL;

Type *parseFile(const char *path, int length) {
    Value cached;
    if (tableGet(&modules, copyString(path, length), &cached)) {
        return AS_OBJ(cached);
    }

    TypeEnvironment *oldEnv = currentEnv;
    currentEnv = NULL;
    TypeEnvironment typeEnvironment;
    initTypeEnvironment(&typeEnvironment, TYPE_SCRIPT);
    initGlobalEnvironment(&typeEnvironment);

    char *source = readFile(path);
    StmtArray *body = parseAST(source);
    evaluateTypes(body);

    SimpleType *type = newSimpleType();
    copyTable(&typeEnvironment.locals, &type->fields);
    // TODO: Importing types
    tableSet(&modules, copyString(path, length), OBJ_VAL(type));

    currentEnv = oldEnv;
    return type;
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

            // TODO: If named type is uninitialized and has no typedef, then
            // infer it here.
            // TODO: If multiple assigns for uninitialized, then make a union
            // Maybe add a var that says whether the type was 'inferred', in which case
            // We can extend the type

            if (!isSubType(valueType, namedType)) {
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
                return (NULL);
            }

            FunctorType *calleeFunctor = calleeType;

            if (casted->arguments.count != calleeFunctor->arguments.count) {
                // TODO: Varargs
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

            if (isSubType(type, listTypeDef)) {
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
            } else if (isSubType(type, mapTypeDef)) {
                GenericType *genericType = (GenericType *) type;
                Type *indexType = evaluateNode(casted->index);
                if (!isSubType(indexType, AS_OBJ(genericType->generics.values[0]))) {
                    error("Key type mismatch");
                    return (NULL);
                }

                if (genericType->generics.count) {
                    return AS_OBJ(genericType->generics.values[1]);
                } else {
                    return neverType;
                }
            } else {
                error("Cannot get item on something other than a list or map");
                return (NULL);
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
            struct Super *casted = (struct Super *) node;
            SimpleType *currentClass = (SimpleType *) currentClassType;
            SimpleType *superType = currentClass->superType;

            Value fieldType;

            if (!tableGet(&superType->methods, copyString(casted->method.start, casted->method.length), &fieldType)) {
                if (!tableGet(&superType->fields, copyString(casted->method.start, casted->method.length),
                              &fieldType)) {
                    errorAt(&casted->method, "Invalid field");
                }
            }

            return AS_TYPE(fieldType);
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

                    tableSet(
                            &currentEnv->locals, copyString(
                                    casted->params.parameters[i]->name.start, casted->params.parameters[i]->name.length
                            ),
                            OBJ_VAL(argType)
                    );
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

            GenericType *type = currentAssignmentType;
            if (currentAssignmentType == NULL) {
                type = newGenericType();
                initValueArray(&type->generics);
                Type *itemType = neverType;
                if (casted->items.count > 0) {
                    if (casted->items.count > 1) {
                        evaluateExprTypes(&casted->items);
                    }

                    itemType = evaluateNode((Node *) casted->items.exprs[0]);
                }
                writeValueArray(&type->generics, OBJ_VAL(itemType));
                type->target = listTypeDef;
            } else {
                if (currentAssignmentType->obj.type != OBJ_PARSE_GENERIC_TYPE) {
                    errorAt(&casted->bracket, "Type mismatch");
                    return type;
                }
                if (!isSubType(listTypeDef, type->target)) {
                    errorAt(&casted->bracket, "Type mismatch, incompatible type");
                    return type;
                }
                if (type->generics.count != 1) {
                    errorAt(&casted->bracket, "Type mismatch, missing type annotation");
                    return type;
                }
                Type *itemType = AS_OBJ(type->generics.values[0]);
                Type *tmp = currentAssignmentType;
                currentAssignmentType = itemType;
                for (int i = 0; i < casted->items.count; i++) {
                    Type *evalType = evaluateNode(casted->items.exprs[i]);
                    if (!isSubType(evalType, itemType)) {
                        errorAt(&casted->bracket, "Type mismatch, incompatible types");
                    }
                }
                currentAssignmentType = tmp;
            }

            return (Type *) type;
        }
        case NODE_MAP: {
            struct Map *casted = (struct Map *) node;

            GenericType *type = currentAssignmentType;
            type->target = mapTypeDef;

            if (currentAssignmentType == NULL) {
                type = newGenericType();
                initValueArray(&type->generics);
                Type *keyType = neverType;
                Type *valueType = neverType;
                if (casted->keys.count > 0) {
                    if (casted->keys.count > 1) {
                        evaluateExprTypes(&casted->keys);
                        evaluateExprTypes(&casted->values);
                    }

                    keyType = evaluateNode((Node *) casted->keys.exprs[0]);
                    valueType = evaluateNode((Node *) casted->values.exprs[0]);
                }
                writeValueArray(&type->generics, OBJ_VAL(keyType));
                writeValueArray(&type->generics, OBJ_VAL(valueType));
                type->target = mapTypeDef;

            } else {
                if (currentAssignmentType->obj.type != OBJ_PARSE_GENERIC_TYPE) {
                    errorAt(&casted->brace, "Type mismatch");
                    return type;
                }
                if (!isSubType(mapTypeDef, type->target)) {
                    errorAt(&casted->brace, "Type mismatch, incompatible type");
                    return type;
                }
                if (type->generics.count != 2) {
                    errorAt(&casted->brace, "Type mismatch, missing type annotation");
                    return type;
                }
                Type *keyType = AS_OBJ(type->generics.values[0]);
                Type *valueType = AS_OBJ(type->generics.values[1]);
                Type *tmp = currentAssignmentType;
                for (int i = 0; i < casted->keys.count; i++) {
                    currentAssignmentType = keyType;
                    Type *evalType = evaluateNode((Node *) casted->keys.exprs[i]);
                    if (!isSubType(evalType, keyType)) {
                        errorAt(&casted->brace, "Map key type mismatch, incompatible types");
                    }
                    currentAssignmentType = valueType;
                    evalType = evaluateNode((Node *) casted->values.exprs[i]);
                    if (!isSubType(evalType, valueType)) {
                        errorAt(&casted->brace, "Map value type mismatch, incompatible types");
                    }
                }
                currentAssignmentType = tmp;
            }
            return (Type *) type;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            return evaluateNode((Node *) casted->expression);
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;
            Type *varType = evaluateNode((Node *) casted->type);

            if (casted->initializer != NULL) {
                Type *oldAssignmentType = currentAssignmentType;
                currentAssignmentType = varType;
                Type *valType = evaluateNode((Node *) casted->initializer);
                if (varType) {
                    if (!isSubType(valType, varType)) {
                        errorAt(&casted->name, "Type mismatch in var");
                    }
                } else {
                    varType = valType;
                }
                currentAssignmentType = oldAssignmentType;
            }

            tableSet(
                    &currentEnv->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(varType)
            );
            return NULL;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            return evaluateBlock(&casted->statements);
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, casted->functionType);

            Type *oldFuncType = currentFuncType;
            FunctorType *type = newFunctorType();
            currentFuncType = type;
            for (int i = 0; i < casted->params.count; i++) {
                TypeNode *typeNode = casted->params.parameters[i]->type;
                Type *argType;
                if (typeNode != NULL) {
                    argType = evaluateNode((Node *) typeNode);
                } else {
                    argType = (Type *) anyType;
                }

                writeValueArray(&type->arguments, OBJ_VAL(argType));

                tableSet(
                        &currentEnv->locals, copyString(
                                casted->params.parameters[i]->name.start, casted->params.parameters[i]->name.length
                        ),
                        OBJ_VAL(argType)
                );

            }

            type->returnType = evaluateNode((Node *) casted->returnType);
            evaluateTypes(&casted->body);
            if (!type->returnType) {
                type->returnType = (Type *) nilType;
            }

            currentEnv = currentEnv->enclosing;

            tableSet(
                    &currentEnv->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(type)
            );

            currentFuncType = oldFuncType;
            return (Type *) type;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;

            SimpleType *classType = newSimpleType();
            Type *oldClass = currentClassType;
            currentClassType = (Type *) classType;
            FunctorType *classFunctionType = newFunctorType();

            classType->superType = NULL;

            if (casted->superclass) {
                SimpleType *superType = getTypeDef(casted->superclass->name);
                copyTable(&superType->fields, &classType->fields);
                copyTable(&superType->methods, &classType->methods);
                classType->superType = (Type *) superType;
            }

            for (int j = 0; j < casted->body.count; j++) {
                if (casted->body.stmts[j]->self.type == NODE_FUNCTION) {
                    struct Function *method = (struct Function *) casted->body.stmts[j];
                    TypeEnvironment typeEnv;
                    initTypeEnvironment(&typeEnv, method->functionType);

                    tableSet(
                            &currentEnv->locals, copyString(
                                    "this", 4
                            ),
                            OBJ_VAL(classType)
                    );

                    FunctorType *type = newFunctorType();
                    FunctorType *oldFuncType = currentFuncType;
                    currentFuncType = type;
                    for (int i = 0; i < method->params.count; i++) {
                        TypeNode *typeNode = method->params.parameters[i]->type;
                        Type *argType;
                        if (typeNode != NULL) {
                            argType = evaluateNode((Node *) typeNode);
                        } else {
                            argType = (Type *) anyType;
                        }

                        writeValueArray(&type->arguments, OBJ_VAL(argType));

                        tableSet(
                                &currentEnv->locals, copyString(
                                        method->params.parameters[i]->name.start,
                                        method->params.parameters[i]->name.length
                                ),
                                OBJ_VAL(argType)
                        );

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

            classFunctionType->returnType = (Type *) classType;

            tableSet(
                    &currentEnv->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(classFunctionType)
            );


            tableSet(
                    &currentEnv->typeDefs, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(classType)
            );

            currentClassType = oldClass;
            return (Type *) classType;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            evaluateNode((Node *) casted->condition);
            Type *result = evaluateNode((Node *) casted->thenBranch);
            evaluateNode((Node *) casted->elseBranch);
            return result;
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
            Type *value = evaluateNode((Node *) casted->value);
            if (currentFuncType->returnType) {
                if (!isSubType(value, currentFuncType->returnType)) {
                    errorAt(&casted->keyword, "Return type mismatch");
                }
            } else {
                currentFuncType->returnType = value;
            }
            return value;
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;
            struct Literal *expr = casted->expression;
            ObjString *str = AS_STRING(expr->value);
            Type *type = parseFile(str->chars, str->length);
            tableSet(
                    &currentEnv->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(type)
            );
            // TODO: Make this actually read the targeted file
            // Don't just accept an expr, accept a string or cast to string literal
            // Also support builtins



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

            if (casted->generics.count > 0) {
                GenericType *genericType = newGenericType();
                genericType->target = type;

                for (int i = 0; i < casted->generics.count; i++) {
                    Type *arg = evaluateNode(casted->generics.typeNodes[i]);
                    writeValueArray(&genericType->generics, OBJ_VAL(arg));
                }
                return genericType;
            }

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
        markTable(&typeEnvironment->locals);
        markTable(&typeEnvironment->typeDefs);
        typeEnvironment = typeEnvironment->enclosing;
    }
}