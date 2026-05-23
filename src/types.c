#include <printf.h>
#include "types.h"
#include "object.h"
#include "vm.h"
#include "libc/list.h"
#include "files.h"
#include "ast/astparse.h"
#include "libc/map.h"
#include "libc/task.h"


Type *evaluateNode(Node *node);

TypeEnvironment *currentEnv = NULL;

SimpleType *newSimpleType() {
    SimpleType *type = ALLOCATE_OBJ(SimpleType, OBJ_PARSE_TYPE);
    push(OBJ_VAL(type));
    initTable(&type->methods);
    initTable(&type->fields);
    initValueArray(&type->genericArgs);
    type->superType = NULL;
    pop();
    return type;
}

FunctorType *newFunctorType() {
    FunctorType *type = ALLOCATE_OBJ(FunctorType, OBJ_PARSE_FUNCTOR_TYPE);
    push(OBJ_VAL(type));
    initValueArray(&type->arguments);
    initValueArray(&type->genericArgs);
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

InterfaceType *newInterfaceType() {
    InterfaceType *type = ALLOCATE_OBJ(InterfaceType, OBJ_PARSE_INTERFACE_TYPE);
    push(OBJ_VAL(type));
    initTable(&type->fields);
    initTable(&type->methods);
    initTable(&type->abstractMethods);
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

GenericTypeDefinition *newGenericTypeDefinition() {
    GenericTypeDefinition *type = ALLOCATE_OBJ(GenericTypeDefinition, OBJ_PARSE_GENERIC_DEFINITION_TYPE);
    type->extends = NULL;
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

static void warnAt(Token *token, const char *message) {
    fprintf(stderr, "[line %d] Warning", token->line);
    if (token->type != TOKEN_EOF && token->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
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
SimpleType *taskTypeDef;

Table modules;
Table builtinModules;

static void defineMethodType(SimpleType *onType, const char *name, Type *returnType) {
    FunctorType *ft = newFunctorType();
    ft->returnType = returnType;
    tableSet(&onType->methods, copyString(name, (int)strlen(name)), OBJ_VAL(ft));
}

void makeTypes() {
    numberType = newSimpleType();
    nilType = newSimpleType();
    boolType = newSimpleType();
    atomType = newSimpleType();
    stringType = newSimpleType();
    neverType = newSimpleType();
    anyType = newSimpleType();

    defineMethodType(stringType, "length", (Type *) numberType);
    defineMethodType(stringType, "split", (Type *) anyType);
    defineMethodType(stringType, "trim", (Type *) stringType);
    defineMethodType(stringType, "contains", (Type *) boolType);
    defineMethodType(stringType, "starts_with", (Type *) boolType);
    defineMethodType(stringType, "ends_with", (Type *) boolType);
    defineMethodType(stringType, "replace", (Type *) stringType);
    defineMethodType(stringType, "to_upper", (Type *) stringType);
    defineMethodType(stringType, "to_lower", (Type *) stringType);
    defineMethodType(stringType, "slice", (Type *) stringType);
    defineMethodType(stringType, "index_of", (Type *) numberType);
    defineMethodType(stringType, "repeat", (Type *) stringType);
    defineMethodType(stringType, "char_at", (Type *) stringType);
    defineMethodType(stringType, "to_number", (Type *) numberType);
    defineMethodType(stringType, "to_string", (Type *) stringType);

    defineMethodType(numberType, "abs", (Type *) numberType);
    defineMethodType(numberType, "floor", (Type *) numberType);
    defineMethodType(numberType, "ceil", (Type *) numberType);
    defineMethodType(numberType, "round", (Type *) numberType);
    defineMethodType(numberType, "to_string", (Type *) stringType);

    defineMethodType(boolType, "to_string", (Type *) stringType);

    listTypeDef = createListTypeDef();
    mapTypeDef = createMapTypeDef();
    taskTypeDef = createTaskTypeDef();

    initTable(&modules);
    initTable(&builtinModules);
}

void defineBuiltinTypeDef(const char *path, const char *name, Type *type, bool builtin) {
    ObjString *pathString = copyString(path, strlen(path));
    tableSet(&modules, pathString, OBJ_VAL(type));

    if (builtin) {
        ObjString *nameString = copyString(name, strlen(name));
        tableSet(&builtinModules, nameString, OBJ_VAL(type));
    }
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

    defineLocal(typeEnvironment, "Number", (Type *) numberType);
    defineLocal(typeEnvironment, "String", (Type *) stringType);
    defineLocal(typeEnvironment, "Bool", (Type *) boolType);
    defineLocal(typeEnvironment, "Nil", (Type *) nilType);
}

void initTypeEnvironment(TypeEnvironment *typeEnvironment, FunctionType type) {
    typeEnvironment->enclosing = currentEnv;
    typeEnvironment->type = type;
    initTable(&typeEnvironment->locals);
    initTable(&typeEnvironment->typeDefs);
    initValueTable(&typeEnvironment->genericResolutions);
    typeEnvironment->scopeDepth = 0;
    currentEnv = typeEnvironment;
}

struct Functor *initFunctor(TypeNodeArray types, TypeNode *returnType, TypeNodeArray generics) {
    struct Functor *type = ALLOCATE_NODE(struct Functor, NODE_FUNCTOR);
    type->arguments = types;
    type->returnType = returnType;
    type->generics = generics;
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
    Value argValue;
    if (arg) {
        return arg;
    } else if (tableGet(&builtinModules, copyString(name.start, name.length), &argValue)) {
        return AS_OBJ(argValue);
    } else {
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

static bool isSubType(Type *subclass, Type *superclass);

static bool resolveGenericArgument(TypeEnvironment *typeEnvironment, Type *subclass, Type *superclass) {
    Value resultValue;
    if (valueTableGet(&typeEnvironment->genericResolutions, OBJ_VAL(superclass), &resultValue)) {
        if (IS_NIL(resultValue)) {
            valueTableSet(&typeEnvironment->genericResolutions, OBJ_VAL(superclass), OBJ_VAL(subclass));
            return true;
        } else {
            return isSubType(subclass, AS_OBJ(resultValue));
        }
    }

    if (!typeEnvironment->enclosing) {
        return false;
    }

    return resolveGenericArgument(typeEnvironment->enclosing, subclass, superclass);
}
static Type* findGenericResolution(TypeEnvironment *typeEnvironment, Type *subclass) {
    Value resultValue;
    if (valueTableGet(&typeEnvironment->genericResolutions, OBJ_VAL(subclass), &resultValue)) {
        return AS_OBJ(resultValue);
    }

    if (!typeEnvironment->enclosing) {
        return NULL;
    }

    return findGenericResolution(typeEnvironment->enclosing, subclass);
}

static Type *resolveType(Type *type) {
    if (type == NULL) return NULL;

    if (type->obj.type == OBJ_PARSE_GENERIC_DEFINITION_TYPE) {
        Type *resolved = findGenericResolution(currentEnv, type);
        if (resolved != NULL) return resolved;
    }

    if (type->obj.type == OBJ_PARSE_GENERIC_TYPE) {
        GenericType *genType = (GenericType *) type;
        bool anyResolved = false;
        for (int i = 0; i < genType->generics.count; i++) {
            Type *arg = AS_OBJ(genType->generics.values[i]);
            Type *resolved = resolveType(arg);
            if (resolved != arg) anyResolved = true;
        }
        if (anyResolved) {
            GenericType *newType = newGenericType();
            newType->target = genType->target;
            for (int i = 0; i < genType->generics.count; i++) {
                Type *resolved = resolveType(AS_OBJ(genType->generics.values[i]));
                writeValueArray(&newType->generics, OBJ_VAL(resolved));
            }
            return (Type *) newType;
        }
    }

    return type;
}

#define MAX_SUBTYPE_STACK 64
typedef struct { Type *sub; Type *super; } SubTypePair;
static SubTypePair subTypeStack[MAX_SUBTYPE_STACK];
static int subTypeStackCount = 0;

static bool isSubTypeInner(Type *subclass, Type *superclass);

static bool isSubType(Type *subclass, Type *superclass) {
    if (subclass == NULL || superclass == NULL) return true;
    if (subclass == superclass) return true;
    if (subTypeStackCount >= MAX_SUBTYPE_STACK) return true;

    for (int i = 0; i < subTypeStackCount; i++) {
        if (subTypeStack[i].sub == subclass && subTypeStack[i].super == superclass) {
            return true;
        }
    }

    subTypeStack[subTypeStackCount].sub = subclass;
    subTypeStack[subTypeStackCount].super = superclass;
    subTypeStackCount++;
    bool result = isSubTypeInner(subclass, superclass);
    subTypeStackCount--;
    return result;
}

static bool isSubTypeInner(Type *subclass, Type *superclass) {

    if (superclass == neverType) {
        return false;
    }

    if (superclass == anyType) {
        return true;
    }

    switch (subclass->obj.type) {
        case (OBJ_PARSE_GENERIC_TYPE): {
            GenericType *subclassType = (GenericType *) subclass;
            if (isSubType(subclassType->target, superclass)) {
                return true;
            }
            break;
        }
        case (OBJ_PARSE_GENERIC_DEFINITION_TYPE): {
            GenericTypeDefinition *subclassType = (GenericTypeDefinition *) subclass;
            Type* inner = findGenericResolution(currentEnv, subclass);
            if (inner) {
                return isSubType(inner, superclass);
            }
            break;
        }
        default: break;
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
                // Contravariant: superclass arg must be subtype of subclass arg
                if (!isSubType(superArgType, subArgType)) {
                    return false;
                }
            }

            return isSubType(subclassType->returnType, superclassType->returnType);
        }
        case (OBJ_PARSE_GENERIC_TYPE): {
            GenericType *superclassType = (GenericType *) superclass;

            if (superclassType->target->obj.type == OBJ_PARSE_INTERFACE_TYPE) {
                InterfaceType *target = (InterfaceType *) superclassType->target;
                if (superclassType->generics.count != target->genericArgs.count) {
                    error("Type argument count mismatch in generic");
                    return false;
                }

                ValueTable snapshot;
                copyValueTable(&currentEnv->genericResolutions, &snapshot);

                for (int i = 0; i < superclassType->generics.count; i++) {
                    valueTableSet(&currentEnv->genericResolutions, target->genericArgs.values[i],
                                  superclassType->generics.values[i]);
                }

                bool result = isSubType(subclass, superclassType->target);
                if (!result) {
                    freeValueTable(&currentEnv->genericResolutions);
                    currentEnv->genericResolutions = snapshot;
                } else {
                    freeValueTable(&snapshot);
                }
                return result;
            }

            if (subclass->obj.type != OBJ_PARSE_GENERIC_TYPE) {
                return false;
            }

            GenericType *subclassType = (GenericType *) subclass;
            if (subclassType->generics.count != superclassType->generics.count) {
                return false;
            }

            for (int i = 0; i < superclassType->generics.count; i++) {
                if (!isSubType(AS_OBJ(subclassType->generics.values[i]), AS_OBJ(superclassType->generics.values[i]))) {
                    return false;
                }
            }

            return isSubType(subclassType->target, superclassType->target);
        }
        case (OBJ_PARSE_GENERIC_DEFINITION_TYPE): {
            GenericTypeDefinition *superclassType = (GenericTypeDefinition *) superclass;
            if (!superclassType->extends || isSubType(subclass, superclassType->extends)) {
                return resolveGenericArgument(currentEnv, subclass, superclass);
            }

            return false;
        }
        case (OBJ_PARSE_UNION_TYPE): {
            UnionType *superclassType = (UnionType *) superclass;
            // Snapshot generic resolutions before speculative left-branch check
            ValueTable snapshot;
            copyValueTable(&currentEnv->genericResolutions, &snapshot);
            if (isSubType(subclass, superclassType->left)) {
                freeValueTable(&snapshot);
                return true;
            }
            // Left failed — restore resolutions before trying right branch
            freeValueTable(&currentEnv->genericResolutions);
            currentEnv->genericResolutions = snapshot;
            return isSubType(subclass, superclassType->right);
        }
        case (OBJ_PARSE_INTERFACE_TYPE): {
            InterfaceType *superclassType = (InterfaceType *) superclass;
            if (subclass->obj.type != OBJ_PARSE_INTERFACE_TYPE && subclass->obj.type != OBJ_PARSE_TYPE) {
                return false;
            }
            InterfaceType *subclassType = (InterfaceType *) subclass;
            for (int i = 0; i < superclassType->fields.count; i++) {
                Entry *entry = &superclassType->fields.entries[i];
                if (entry->key != NULL) {
                    Type *fieldType = AS_OBJ(entry->value);
                    Value targetFieldValue;
                    if (!tableGet(&subclassType->fields, entry->key, &targetFieldValue)) {
                        return false;
                    }

                    if (!isSubType(AS_OBJ(targetFieldValue), fieldType)) {
                        return false;
                    }
                }
            }
            for (int i = 0; i < superclassType->methods.count; i++) {
                Entry *entry = &superclassType->methods.entries[i];
                if (entry->key != NULL) {
                    Type *methodType = AS_OBJ(entry->value);
                    Value targetMethodValue;
                    if (!tableGet(&subclassType->methods, entry->key, &targetMethodValue)) {
                        return false;
                    }

                    if (!isSubType(AS_OBJ(targetMethodValue), methodType)) {
                        return false;
                    }
                }
            }
            return true;
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
        panicMode = false;
        evaluateNode((Node *) statements->stmts[i]);
    }
}

Type *evaluateBlock(StmtArray *statements) {
    Type *last = NULL;
    for (int i = 0; i < statements->count; i++) {
        last = evaluateNode((Node *) statements->stmts[i]);
    }
    return last;
}

static void preRegisterDeclarations(StmtArray *statements) {
    for (int i = 0; i < statements->count; i++) {
        Node *node = (Node *) statements->stmts[i];
        if (node->type == NODE_FUNCTION) {
            struct Function *fn = (struct Function *) node;
            FunctorType *placeholder = newFunctorType();
            placeholder->returnType = (Type *) anyType;
            tableSet(&currentEnv->locals, copyString(fn->name.start, fn->name.length), OBJ_VAL(placeholder));
        } else if (node->type == NODE_CLASS) {
            struct Class *cls = (struct Class *) node;
            SimpleType *classPlaceholder = newSimpleType();
            FunctorType *ctorPlaceholder = newFunctorType();
            ctorPlaceholder->returnType = (Type *) classPlaceholder;
            tableSet(&currentEnv->locals, copyString(cls->name.start, cls->name.length), OBJ_VAL(ctorPlaceholder));
            tableSet(&currentEnv->typeDefs, copyString(cls->name.start, cls->name.length), OBJ_VAL(classPlaceholder));
        } else if (node->type == NODE_ENUM) {
            struct Enum *en = (struct Enum *) node;
            SimpleType *placeholder = newSimpleType();
            tableSet(&currentEnv->locals, copyString(en->name.start, en->name.length), OBJ_VAL(placeholder));
            tableSet(&currentEnv->typeDefs, copyString(en->name.start, en->name.length), OBJ_VAL(placeholder));
        } else if (node->type == NODE_INTERFACE) {
            struct Interface *iface = (struct Interface *) node;
            InterfaceType *placeholder = newInterfaceType();
            tableSet(&currentEnv->locals, copyString(iface->name.start, iface->name.length), OBJ_VAL(placeholder));
            tableSet(&currentEnv->typeDefs, copyString(iface->name.start, iface->name.length), OBJ_VAL(placeholder));
        }
    }
}

void evaluateTree(StmtArray *statements) {
    TypeEnvironment typeEnv;
    initTypeEnvironment(&typeEnv, TYPE_SCRIPT);
    initGlobalEnvironment(&typeEnv);
    currentEnv = &typeEnv;
    preRegisterDeclarations(statements);
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

    char *resolved = findModule(path, NULL);
    char *source = readFile(resolved);
    if (source == NULL) {
        return NULL;
    }

    TypeEnvironment *oldEnv = currentEnv;
    currentEnv = NULL;
    TypeEnvironment typeEnvironment;
    initTypeEnvironment(&typeEnvironment, TYPE_SCRIPT);
    initGlobalEnvironment(&typeEnvironment);

    bool oldHadError = hadError;
    bool oldPanicMode = panicMode;
    hadError = false;
    panicMode = false;

    StmtArray *body = parseAST(source);
    evaluateTypes(body);

    hadError = oldHadError;
    panicMode = oldPanicMode;

    SimpleType *type = newSimpleType();
    copyTable(&typeEnvironment.locals, &type->fields);
    copyTable(&typeEnvironment.typeDefs, &type->methods);
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
            Type *leftType = evaluateNode((Node *) casted->left);
            evaluateNode((Node *) casted->right);

            const char *opMethod = NULL;
            bool isBoolOp = false;

            switch (casted->operator.type) {
                case TOKEN_IS:
                    return (Type *) boolType;
                case TOKEN_PLUS: opMethod = "add"; break;
                case TOKEN_MINUS: opMethod = "sub"; break;
                case TOKEN_STAR: opMethod = "mul"; break;
                case TOKEN_SLASH: opMethod = "div"; break;
                case TOKEN_MODULO: opMethod = "mod"; break;
                case TOKEN_LESS: opMethod = "lt"; isBoolOp = true; break;
                case TOKEN_GREATER: opMethod = "gt"; isBoolOp = true; break;
                case TOKEN_LESS_EQUAL: opMethod = "lt"; isBoolOp = true; break;
                case TOKEN_GREATER_EQUAL: opMethod = "gt"; isBoolOp = true; break;
                case TOKEN_EQUAL_EQUAL: opMethod = "eq"; isBoolOp = true; break;
                case TOKEN_BANG_EQUAL: opMethod = "eq"; isBoolOp = true; break;
                default: return leftType;
            }

            if (isBoolOp && (leftType == (Type *) numberType || leftType == (Type *) stringType)) {
                return (Type *) boolType;
            }
            if (!isBoolOp && (leftType == (Type *) numberType || leftType == (Type *) stringType)) {
                return leftType;
            }

            if (leftType != NULL && opMethod != NULL &&
                (leftType->obj.type == OBJ_PARSE_TYPE || leftType->obj.type == OBJ_PARSE_INTERFACE_TYPE)) {
                SimpleType *st = (SimpleType *) leftType;
                Value methodVal;
                ObjString *methodName = copyString(opMethod, strlen(opMethod));
                if (tableGet(&st->methods, methodName, &methodVal)) {
                    Type *methodType = AS_OBJ(methodVal);
                    if (methodType->obj.type == OBJ_PARSE_FUNCTOR_TYPE) {
                        Type *ret = resolveType(((FunctorType *) methodType)->returnType);
                        return isBoolOp ? (Type *) boolType : ret;
                    }
                }
            }

            return isBoolOp ? (Type *) boolType : leftType;
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

            if (calleeType == (Type *) anyType) {
                for (int i = 0; i < casted->arguments.count; i++) {
                    evaluateNode((Node *) casted->arguments.exprs[i]);
                }
                return (Type *) anyType;
            }
            if (calleeType == NULL || calleeType->obj.type != OBJ_PARSE_FUNCTOR_TYPE) {
                if (calleeType != NULL) errorAt(&casted->paren, "Type is not callable");
                return (NULL);
            }

            FunctorType *calleeFunctor = calleeType;

            if (casted->arguments.count != calleeFunctor->arguments.count) {
                // TODO: Varargs
//                errorAt(&casted->paren, "Too many arguments provided");
//                return(NULL);
            }

            TypeEnvironment argEnv;
            initTypeEnvironment(&argEnv, TYPE_FUNCTION);

            for (int i = 0; i < calleeFunctor->genericArgs.count; i++) {
                valueTableSet(&argEnv.genericResolutions, calleeFunctor->genericArgs.values[i], NIL_VAL);
            }

            for (int i = 0; i < casted->arguments.count; i++) {
                Type *oldAssignmentType = currentAssignmentType;
                if (i < calleeFunctor->arguments.count) {
                    currentAssignmentType = AS_OBJ(calleeFunctor->arguments.values[i]);
                }
                Type *argType = evaluateNode((Node *) casted->arguments.exprs[i]);
                currentAssignmentType = oldAssignmentType;
                if (i < calleeFunctor->arguments.count && !isSubType(argType, AS_OBJ(calleeFunctor->arguments.values[i]))) {
                    errorAt(&casted->paren, "Type mismatch");
                    return NULL;
                }
            }

            Type *returnType = resolveType(calleeFunctor->returnType);

            currentEnv = currentEnv->enclosing;
            return returnType;
        }
        case NODE_GETITEM: {
            struct GetItem *casted = (struct GetItem *) node;
            Type *type = evaluateNode((Node *) casted->object);

            if (isSubType(type, listTypeDef)) {
                GenericType *genericType = (GenericType *) type;
                Type *indexType = evaluateNode(casted->index);
                if (!isSubType(indexType, numberType)) {
                    errorAt(&casted->bracket, "Index must be a number");
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
                    errorAt(&casted->bracket, "Key type mismatch");
                    return (NULL);
                }

                if (genericType->generics.count) {
                    return AS_OBJ(genericType->generics.values[1]);
                } else {
                    return neverType;
                }
            } else {
                errorAt(&casted->bracket, "Cannot get item on something other than a list or map");
                return (NULL);
            }
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            Type *objectType = evaluateNode((Node *) casted->object);
            if (objectType == NULL) return NULL;
            if (objectType == (Type *) anyType) return (Type *) anyType;
            SimpleType *rootType;

            switch (objectType->obj.type) {
                case OBJ_PARSE_TYPE:
                case OBJ_PARSE_INTERFACE_TYPE: {
                    rootType = (SimpleType *) objectType;
                    break;
                }
                case OBJ_PARSE_GENERIC_TYPE: {
                    GenericType *genType = (GenericType *) objectType;
                    rootType = (SimpleType *) genType->target;
                    ValueArray *targetGenericArgs = NULL;
                    if (genType->target->obj.type == OBJ_PARSE_INTERFACE_TYPE) {
                        targetGenericArgs = &((InterfaceType *) genType->target)->genericArgs;
                    } else if (genType->target->obj.type == OBJ_PARSE_TYPE) {
                        targetGenericArgs = &((SimpleType *) genType->target)->genericArgs;
                    }
                    if (targetGenericArgs != NULL) {
                        for (int i = 0; i < targetGenericArgs->count && i < genType->generics.count; i++) {
                            valueTableSet(&currentEnv->genericResolutions,
                                          targetGenericArgs->values[i],
                                          genType->generics.values[i]);
                        }
                    }
                    break;
                }
                case OBJ_PARSE_GENERIC_DEFINITION_TYPE: {
                    GenericTypeDefinition *genDef = (GenericTypeDefinition *) objectType;
                    if (!genDef->extends) {
                        errorAt(&casted->name, "Attempting to get from invalid generic type.");
                        return NULL;
                    }
                    if (genDef->extends->obj.type == OBJ_PARSE_GENERIC_TYPE) {
                        GenericType *genType = (GenericType *) genDef->extends;
                        rootType = (SimpleType *) genType->target;
                        ValueArray *targetGenericArgs = NULL;
                        if (genType->target->obj.type == OBJ_PARSE_INTERFACE_TYPE) {
                            targetGenericArgs = &((InterfaceType *) genType->target)->genericArgs;
                        } else if (genType->target->obj.type == OBJ_PARSE_TYPE) {
                            targetGenericArgs = &((SimpleType *) genType->target)->genericArgs;
                        }
                        if (targetGenericArgs != NULL) {
                            for (int i = 0; i < targetGenericArgs->count && i < genType->generics.count; i++) {
                                valueTableSet(&currentEnv->genericResolutions,
                                              targetGenericArgs->values[i],
                                              genType->generics.values[i]);
                            }
                        }
                    } else {
                        rootType = (SimpleType *) genDef->extends;
                    }
                    break;
                }
                default: {
                    errorAt(&casted->name, "Attempting to get from invalid type.");
                }
            }

            Value fieldType;
            ObjString *nameString = copyString(casted->name.start, casted->name.length);

            if (rootType == anyType) return (Type *) anyType;

            if (!tableGet(&rootType->methods, nameString, &fieldType)) {
                if (!tableGet(&rootType->fields, nameString, &fieldType)) {
                    errorAt(&casted->name, "Invalid field");
                    return NULL;
                }
            }

            return resolveType(AS_TYPE(fieldType));
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            Type *valueType = evaluateNode((Node *) casted->value);

            Type *objectType = evaluateNode((Node *) casted->object);
            SimpleType *rootType = (SimpleType *) objectType;

            if (objectType->obj.type == OBJ_PARSE_GENERIC_TYPE) {
                rootType = (SimpleType *) ((GenericType *) objectType)->target;
            }

            if (rootType == anyType) return (Type *) anyType;

            Value fieldType;

            if (!tableGet(&rootType->methods, copyString(casted->name.start, casted->name.length), &fieldType)) {
                if (!tableGet(&rootType->fields, copyString(casted->name.start, casted->name.length), &fieldType)) {
                    errorAt(&casted->name, "Invalid field");
                }
            }

            if (!isSubType(valueType, AS_TYPE(fieldType))) {
                errorAt(&casted->name, "Type mismatch in setter");
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

            ValueArray genericArgs;
            initValueArray(&genericArgs);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                Type *extendType = typeNode->target != NULL ? evaluateNode((Node *) typeNode->target) : NULL;
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->extends = extendType;
                argType->name = typeNode->name;

                writeValueArray(&genericArgs, OBJ_VAL(argType));

                tableSet(
                        &typeEnv.typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

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
                    Type *argType = (Type *) anyType;
                    // Infer from call-site context if available
                    if (currentAssignmentType != NULL &&
                        currentAssignmentType->obj.type == OBJ_PARSE_FUNCTOR_TYPE) {
                        FunctorType *expectedFunctor = (FunctorType *) currentAssignmentType;
                        if (i < expectedFunctor->arguments.count) {
                            argType = AS_OBJ(expectedFunctor->arguments.values[i]);
                        }
                    }
                    if (argType == (Type *) anyType) {
                        warnAt(&casted->params.parameters[i]->name, "Missing type annotation, defaulting to Any");
                    }
                    writeValueArray(&type->arguments, OBJ_VAL(argType));

                    tableSet(
                            &currentEnv->locals, copyString(
                                    casted->params.parameters[i]->name.start, casted->params.parameters[i]->name.length
                            ),
                            OBJ_VAL(argType)
                    );
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
            if (currentAssignmentType == NULL || currentAssignmentType == (Type *) anyType) {
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

            if (currentAssignmentType == NULL || currentAssignmentType == (Type *) anyType) {
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
                        isSubType(valType, varType);
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

            ValueArray genericArgs;
            initValueArray(&genericArgs);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                Type *extendType = typeNode->target != NULL ? evaluateNode((Node *) typeNode->target) : NULL;
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->extends = extendType;
                argType->name = typeNode->name;

                writeValueArray(&genericArgs, OBJ_VAL(argType));

                tableSet(
                        &typeEnv.typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

            Type *oldFuncType = currentFuncType;
            FunctorType *type = newFunctorType();
            type->genericArgs = genericArgs;
            currentFuncType = type;
            for (int i = 0; i < casted->params.count; i++) {
                TypeNode *typeNode = casted->params.parameters[i]->type;
                Type *argType;
                if (typeNode != NULL) {
                    argType = evaluateNode((Node *) typeNode);
                } else {
                    warnAt(&casted->params.parameters[i]->name, "Missing type annotation, defaulting to Any");
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

            // Pre-register in enclosing scope for recursive calls
            tableSet(
                    &currentEnv->enclosing->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(type)
            );

            evaluateTypes(&casted->body);
            if (!type->returnType) {
                type->returnType = (Type *) nilType;
            }

            currentEnv = currentEnv->enclosing;
            currentFuncType = oldFuncType;
            return (Type *) type;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;

            // Reuse pre-registered placeholder if it exists
            SimpleType *classType = NULL;
            Value existingType;
            if (tableGet(&currentEnv->typeDefs, copyString(casted->name.start, casted->name.length), &existingType)) {
                Type *existing = AS_OBJ(existingType);
                if (existing->obj.type == OBJ_PARSE_TYPE) {
                    classType = (SimpleType *) existing;
                }
            }
            if (classType == NULL) classType = newSimpleType();
            Type *oldClass = currentClassType;
            currentClassType = (Type *) classType;
            FunctorType *classFunctionType = newFunctorType();

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_INITIALIZER);

            ValueArray genericArgs;
            initValueArray(&genericArgs);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                Type *extendType = typeNode->target != NULL ? evaluateNode((Node *) typeNode->target) : NULL;
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->extends = extendType;
                argType->name = typeNode->name;

                writeValueArray(&genericArgs, OBJ_VAL(argType));

                tableSet(
                        &typeEnv.typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

            classType->superType = NULL;
            classType->genericArgs = genericArgs;

            // First pass: copy from class parents only (not interfaces)
            for (int i = 0; i < casted->superclasses.count; i++) {
                struct Variable *superVar = (struct Variable *) casted->superclasses.exprs[i];
                Type *superType = getTypeDef(superVar->name);
                if (superType && superType->obj.type == OBJ_PARSE_TYPE) {
                    SimpleType *st = (SimpleType *) superType;
                    tableAddAll(&st->fields, &classType->fields);
                    tableAddAll(&st->methods, &classType->methods);
                    if (i == 0) {
                        classType->superType = superType;
                    }
                }
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
                            warnAt(&method->params.parameters[i]->name, "Missing type annotation, defaulting to Any");
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

            currentEnv = currentEnv->enclosing;

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

            // Interface conformance: check abstract methods, then copy defaults
            for (int i = 0; i < casted->superclasses.count; i++) {
                struct Variable *superVar = (struct Variable *) casted->superclasses.exprs[i];
                Type *superType = getTypeDef(superVar->name);
                if (superType && superType->obj.type == OBJ_PARSE_INTERFACE_TYPE) {
                    InterfaceType *iface = (InterfaceType *) superType;
                    for (int j = 0; j < iface->abstractMethods.capacity; j++) {
                        Entry *entry = &iface->abstractMethods.entries[j];
                        if (entry->key != NULL) {
                            Value impl;
                            if (!tableGet(&classType->methods, entry->key, &impl)) {
                                fprintf(stderr, "[line %d] Error at '%.*s': Missing implementation for '%s' from interface\n",
                                        casted->name.line, casted->name.length, casted->name.start, entry->key->chars);
                                hadError = true;
                            }
                        }
                    }
                    // Copy interface methods (defaults) that class doesn't override
                    tableAddAll(&iface->fields, &classType->fields);
                    tableAddAll(&iface->methods, &classType->methods);
                    if (classType->superType == NULL) {
                        classType->superType = superType;
                    }
                }
            }

            currentClassType = oldClass;
            return (Type *) classType;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            evaluateNode((Node *) casted->condition);

            // Flow narrowing: if condition is `x is Type`, narrow x in branches
            Expr *cond = casted->condition;
            Token *narrowVar = NULL;
            Type *narrowType = NULL;
            Type *originalType = NULL;

            if (cond->type == NODE_BINARY) {
                struct Binary *binCond = (struct Binary *) cond;
                if (binCond->operator.type == TOKEN_IS &&
                    binCond->left->type == NODE_VARIABLE &&
                    binCond->right->type == NODE_VARIABLE) {
                    struct Variable *lhs = (struct Variable *) binCond->left;
                    struct Variable *rhs = (struct Variable *) binCond->right;
                    narrowVar = &lhs->name;
                    narrowType = getTypeDef(rhs->name);
                    originalType = getVariableType(lhs->name);
                }
            }

            Type *result;
            if (narrowVar && narrowType) {
                // Then-branch: variable has the narrowed type
                TypeEnvironment thenEnv;
                initTypeEnvironment(&thenEnv, currentEnv->type);
                tableSet(&thenEnv.locals, copyString(narrowVar->start, narrowVar->length), OBJ_VAL(narrowType));
                result = evaluateNode((Node *) casted->thenBranch);
                currentEnv = currentEnv->enclosing;

                // Else-branch: subtract narrowed type from union if applicable
                if (casted->elseBranch) {
                    Type *elseType = originalType;
                    if (originalType && originalType->obj.type == OBJ_PARSE_UNION_TYPE) {
                        UnionType *u = (UnionType *) originalType;
                        if (isSubType(u->left, narrowType) && isSubType(narrowType, u->left)) {
                            elseType = u->right;
                        } else if (isSubType(u->right, narrowType) && isSubType(narrowType, u->right)) {
                            elseType = u->left;
                        }
                    }
                    TypeEnvironment elseEnv;
                    initTypeEnvironment(&elseEnv, currentEnv->type);
                    if (elseType != originalType) {
                        tableSet(&elseEnv.locals, copyString(narrowVar->start, narrowVar->length), OBJ_VAL(elseType));
                    }
                    evaluateNode((Node *) casted->elseBranch);
                    currentEnv = currentEnv->enclosing;
                }
            } else {
                result = evaluateNode((Node *) casted->thenBranch);
                evaluateNode((Node *) casted->elseBranch);
            }
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
                    if (!isSubType(currentFuncType->returnType, value)) {
                        // Neither direction works — widen to a union
                        UnionType *union_ = newUnionType();
                        union_->left = currentFuncType->returnType;
                        union_->right = value;
                        currentFuncType->returnType = (Type *) union_;
                    } else {
                        // value is a supertype of current — widen
                        currentFuncType->returnType = value;
                    }
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
            if (type == NULL) {
                errorAt(&casted->name, "Could not resolve import");
                return NULL;
            }
            tableSet(
                    &currentEnv->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(type)
            );

            return NULL;
        }
        case NODE_FUNCTOR: {
            struct Functor *casted = (struct Functor *) node;
            FunctorType *type = newFunctorType();

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_FUNCTION);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->name = typeNode->name;
                writeValueArray(&type->genericArgs, OBJ_VAL(argType));
                tableSet(
                        &currentEnv->typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

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

            currentEnv = currentEnv->enclosing;

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
        case NODE_INTERFACE: {
            struct Interface *casted = (struct Interface *) node;
            InterfaceType *interfaceType = newInterfaceType();
            interfaceType->superType = NULL;

            tableSet(
                    &currentEnv->typeDefs, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(interfaceType)
            );
            tableSet(
                    &currentEnv->locals, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(interfaceType)
            );

            if (casted->superType) {
                InterfaceType *superType = getTypeDef(casted->superType->name);

                if (superType->self.obj.type != OBJ_PARSE_INTERFACE_TYPE) {
                    errorAt(&casted->superType->name, "Parent type for interface may only be an interface.");
                    return NULL;
                }

                copyTable(&superType->fields, &interfaceType->fields);
                copyTable(&superType->methods, &interfaceType->methods);
                interfaceType->superType = (Type *) superType;
            }

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_INITIALIZER);

            ValueArray genericArgs;
            initValueArray(&genericArgs);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                Type *extendType = typeNode->target != NULL ? evaluateNode((Node *) typeNode->target) : NULL;
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->extends = extendType;
                argType->name = typeNode->name;

                writeValueArray(&genericArgs, OBJ_VAL(argType));

                tableSet(
                        &typeEnv.typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

            interfaceType->genericArgs = genericArgs;

            for (int j = 0; j < casted->body.count; j++) {
                if (casted->body.stmts[j]->self.type == NODE_METHODSIG) {
                    struct MethodSig *method = (struct MethodSig *) casted->body.stmts[j];

                    FunctorType *type = newFunctorType();
                    for (int i = 0; i < method->params.count; i++) {
                        TypeNode *typeNode = method->params.parameters[i]->type;
                        Type *argType;
                        if (typeNode != NULL) {
                            argType = evaluateNode((Node *) typeNode);
                        } else {
                            argType = (Type *) anyType;
                        }

                        writeValueArray(&type->arguments, OBJ_VAL(argType));
                    }

                    ObjString *methodName = copyString(method->name.start, method->name.length);
                    tableSet(&interfaceType->methods, methodName, OBJ_VAL(type));
                    tableSet(&interfaceType->abstractMethods, methodName, BOOL_VAL(true));

                    if (method->functionType != TYPE_INITIALIZER) {
                        type->returnType = evaluateNode((Node *) method->returnType);
                    } else {
                        type->returnType = (Type *) interfaceType;
                    }

                    if (!type->returnType) {
                        type->returnType = (Type *) nilType;
                    }
                } else if (casted->body.stmts[j]->self.type == NODE_FUNCTION) {
                    struct Function *method = (struct Function *) casted->body.stmts[j];

                    FunctorType *type = newFunctorType();
                    for (int i = 0; i < method->params.count; i++) {
                        TypeNode *typeNode = method->params.parameters[i]->type;
                        Type *argType;
                        if (typeNode != NULL) {
                            argType = evaluateNode((Node *) typeNode);
                        } else {
                            argType = (Type *) anyType;
                        }
                        writeValueArray(&type->arguments, OBJ_VAL(argType));
                    }

                    type->returnType = evaluateNode((Node *) method->returnType);
                    if (!type->returnType) {
                        type->returnType = (Type *) nilType;
                    }

                    tableSet(
                            &interfaceType->methods,
                            copyString(method->name.start, method->name.length),
                            OBJ_VAL(type)
                    );
                } else {
                    struct Var *var = (struct Var *) casted->body.stmts[j];
                    Type *type = evaluateNode((Node *) var->type);
                    tableSet(
                            &interfaceType->fields,
                            copyString(var->name.start, var->name.length),
                            OBJ_VAL(type)
                    );
                }
            }

            currentEnv = currentEnv->enclosing;

            break;
        }
        case NODE_TYPEDECLARATION: {
            struct TypeDeclaration *casted = (struct TypeDeclaration *) node;

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_INITIALIZER);

            ValueArray genericArgs;
            initValueArray(&genericArgs);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                Type *extendType = typeNode->target != NULL ? evaluateNode((Node *) typeNode->target) : NULL;
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->extends = extendType;
                argType->name = typeNode->name;

                writeValueArray(&genericArgs, OBJ_VAL(argType));

                tableSet(
                        &typeEnv.typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

            Type *result = evaluateNode(casted->target);
            currentEnv = currentEnv->enclosing;

            tableSet(
                    &currentEnv->typeDefs, copyString(
                            casted->name.start, casted->name.length
                    ),
                    OBJ_VAL(result)
            );

            break;
        }
        case NODE_ENUM: {
            struct Enum *casted = (struct Enum *) node;
            SimpleType *enumType = newSimpleType();

            TypeEnvironment typeEnv;
            initTypeEnvironment(&typeEnv, TYPE_INITIALIZER);

            ValueArray genericArgs;
            initValueArray(&genericArgs);

            for (int i = 0; i < casted->generics.count; i++) {
                struct TypeDeclaration *typeNode = casted->generics.typeNodes[i];
                Type *extendType = typeNode->target != NULL ? evaluateNode((Node *) typeNode->target) : NULL;
                GenericTypeDefinition *argType = newGenericTypeDefinition();
                argType->extends = extendType;
                argType->name = typeNode->name;

                writeValueArray(&genericArgs, OBJ_VAL(argType));

                tableSet(
                        &typeEnv.typeDefs, copyString(
                                typeNode->name.start, typeNode->name.length
                        ),
                        OBJ_VAL(argType)
                );
            }

            enumType->genericArgs = genericArgs;

            for (int i = 0; i < casted->body.count; i++) {
                struct EnumItem *variant = (struct EnumItem *) casted->body.stmts[i];

                if (variant->params.count == 0) {
                    // Zero-arg variant: value IS the enum type directly
                    tableSet(&enumType->methods,
                             copyString(variant->name.start, variant->name.length),
                             OBJ_VAL(enumType));
                } else {
                    FunctorType *variantType = newFunctorType();
                    variantType->genericArgs = genericArgs;

                    for (int j = 0; j < variant->params.count; j++) {
                        TypeNode *typeNode = variant->params.parameters[j]->type;
                        Type *argType;
                        if (typeNode != NULL) {
                            argType = evaluateNode((Node *) typeNode);
                        } else {
                            argType = (Type *) anyType;
                        }
                        writeValueArray(&variantType->arguments, OBJ_VAL(argType));
                    }

                    variantType->returnType = (Type *) enumType;
                    tableSet(&enumType->methods,
                             copyString(variant->name.start, variant->name.length),
                             OBJ_VAL(variantType));
                }
            }

            currentEnv = currentEnv->enclosing;

            tableSet(&currentEnv->locals,
                     copyString(casted->name.start, casted->name.length),
                     OBJ_VAL(enumType));
            tableSet(&currentEnv->typeDefs,
                     copyString(casted->name.start, casted->name.length),
                     OBJ_VAL(enumType));
            return (Type *) enumType;
        }
        case NODE_ENUMITEM: {
            break;
        }
        case NODE_MATCH: {
            struct Match *casted = (struct Match *) node;
            Type *subjectType = evaluateNode((Node *) casted->subject);
            // Collect matched variant names for exhaustiveness check
            Table matchedVariants;
            initTable(&matchedVariants);
            Type *resultType = NULL;

            for (int i = 0; i < casted->arms.count; i++) {
                struct MatchArm *arm = (struct MatchArm *) casted->arms.stmts[i];

                // Type check the arm body with bindings in scope
                TypeEnvironment armEnv;
                initTypeEnvironment(&armEnv, currentEnv->type);

                if (arm->isTypePattern) {
                    // is Type(binding) => ...
                    Type *narrowedType = getTypeDef(arm->variantName);
                    if (narrowedType && arm->bindings.count > 0) {
                        tableSet(&armEnv.locals,
                                 copyString(arm->bindings.parameters[0]->name.start,
                                            arm->bindings.parameters[0]->name.length),
                                 OBJ_VAL(narrowedType));
                    }
                } else {
                    tableSet(&matchedVariants,
                             copyString(arm->variantName.start, arm->variantName.length),
                             BOOL_VAL(true));

                    // Look up variant constructor to type the bindings
                    if (subjectType && subjectType->obj.type == OBJ_PARSE_TYPE) {
                        SimpleType *enumST = (SimpleType *) subjectType;
                        Value variantVal;
                        if (tableGet(&enumST->methods,
                                     copyString(arm->variantName.start, arm->variantName.length),
                                     &variantVal)) {
                            Type *variantType = AS_OBJ(variantVal);
                            if (variantType->obj.type == OBJ_PARSE_FUNCTOR_TYPE && arm->bindings.count > 0) {
                                FunctorType *variantFn = (FunctorType *) variantType;
                                for (int j = 0; j < arm->bindings.count && j < variantFn->arguments.count; j++) {
                                    Type *bindingType = AS_OBJ(variantFn->arguments.values[j]);
                                    tableSet(&armEnv.locals,
                                             copyString(arm->bindings.parameters[j]->name.start,
                                                        arm->bindings.parameters[j]->name.length),
                                             OBJ_VAL(bindingType));
                                }
                            }
                        } else {
                            errorAt(&arm->variantName, "Unknown variant in match");
                        }
                    }
                }

                Type *armResult = NULL;
                for (int j = 0; j < arm->body.count; j++) {
                    armResult = evaluateNode((Node *) arm->body.stmts[j]);
                }

                if (resultType == NULL) {
                    resultType = armResult;
                } else if (armResult && !isSubType(armResult, resultType)) {
                    if (!isSubType(resultType, armResult)) {
                        UnionType *u = newUnionType();
                        u->left = resultType;
                        u->right = armResult;
                        resultType = (Type *) u;
                    } else {
                        resultType = armResult;
                    }
                }

                currentEnv = currentEnv->enclosing;
            }

            // Exhaustiveness check: only for enum variant patterns (not is-type patterns)
            bool hasIsPattern = false;
            for (int i = 0; i < casted->arms.count; i++) {
                if (((struct MatchArm *)casted->arms.stmts[i])->isTypePattern) {
                    hasIsPattern = true;
                    break;
                }
            }
            if (!hasIsPattern && subjectType && subjectType->obj.type == OBJ_PARSE_TYPE) {
                SimpleType *enumST = (SimpleType *) subjectType;
                for (int i = 0; i < enumST->methods.capacity; i++) {
                    Entry *entry = &enumST->methods.entries[i];
                    if (entry->key != NULL) {
                        Value dummy;
                        if (!tableGet(&matchedVariants, entry->key, &dummy)) {
                            Token varToken = syntheticToken(entry->key->chars);
                            struct Match *m = casted;
                            errorAt(&m->arms.count > 0
                                ? &((struct MatchArm *)m->arms.stmts[0])->variantName
                                : &varToken,
                                "Non-exhaustive match: missing variant");
                        }
                    }
                }
            }

            freeTable(&matchedVariants);
            return resultType;
        }
        case NODE_MATCHARM: {
            break;
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
    markTable(&modules);
    TypeEnvironment *typeEnvironment = currentEnv;
    while (typeEnvironment != NULL) {
        markTable(&typeEnvironment->locals);
        markTable(&typeEnvironment->typeDefs);
        typeEnvironment = typeEnvironment->enclosing;
    }
}