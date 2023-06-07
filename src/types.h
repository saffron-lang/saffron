#ifndef CRAFTING_INTERPRETERS_TYPES_H
#define CRAFTING_INTERPRETERS_TYPES_H

#include "ast/ast.h"
#include "table.h"
#include "object.h"

struct Functor *initFunctor(TypeNodeArray types, TypeNode *returnType);

struct Simple *initSimple(Token name);

void evaluateTree(StmtArray *statements);

#define AS_TYPE(value)       (((Type *)AS_OBJ(value)))

typedef struct Type {
    Obj obj;
} Type;

typedef struct SimpleType {
    Type self;
    Table fields;
    Table methods;
    int genericCount;
    Type *superType;
} SimpleType;

typedef struct FunctorType {
    int genericCount;
    ValueArray arguments;
    Type *returnType;
} FunctorType;

typedef struct GenericType {
    Type self;
    Type *target;
    ValueArray generics;
} GenericType;

typedef struct UnionType {
    Type self;
    Type *left;
    Type *right;
} UnionType;

typedef struct InterfaceType {
    Table fields;
    Table methods;
    int genericCount;
} TypeUnion;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
    Type *type;
} TypeLocal;

// Todo: Maybe some sort of type for intrinsic simple types
// Or maybe just recursive compare all children and provide more complex
// types for builtins / primitives.

// Should 2 type equivalent classes be comparable? Probably not on type alone.
// Also todo: Inheritance, this, super, class attributes, instance attributes, generic types

typedef struct TypeEnvironment {
    struct TypeEnvironment *enclosing;
    FunctionType type;
    TypeLocal locals[UINT8_COUNT];
    int localCount;
    TypeLocal typeDefs[UINT8_COUNT];
    int typeDefCount;
    int scopeDepth;
} TypeEnvironment;

SimpleType *newSimpleType();
FunctorType *newFunctorType();
UnionType *newUnionType();

GenericType *newGenericType();

SimpleType *numberType;
SimpleType *boolType;
SimpleType *nilType;
SimpleType *atomType;
SimpleType *stringType;
SimpleType *neverType;
SimpleType *listTypeDef;

#endif //CRAFTING_INTERPRETERS_TYPES_H
