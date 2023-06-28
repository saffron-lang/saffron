#ifndef CRAFTING_INTERPRETERS_TYPES_H
#define CRAFTING_INTERPRETERS_TYPES_H

#include "ast/ast.h"
#include "table.h"
#include "object.h"
#include "valuetable.h"

struct Functor *initFunctor(TypeNodeArray types, TypeNode *returnType, TypeNodeArray generics);

struct Simple *initSimple(Token name);

void evaluateTree(StmtArray *statements);

#define AS_TYPE(value)       (((Type *)AS_OBJ(value)))

typedef struct Type {
    Obj obj;
} Type;

typedef struct SimpleType {
    // Number
    Type self;
    Table fields;
    Table methods;
    ValueArray genericArgs;
    Type *superType;
} SimpleType;

typedef struct FunctorType {
    // (a: Number) => String
    Type self;
    ValueArray arguments;
    ValueArray genericArgs;
    Type *returnType;
} FunctorType;

typedef struct GenericType {
    // List<Number>
    Type self;
    Type *target;
    ValueArray generics;
    // TODO: Make this check against the extends of the type def
    // And error if the target type doesn't support generics
    // E.g. if its a union type or already a generic
} GenericType;

typedef struct UnionType {
    // Number | String
    Type self;
    Type *left;
    Type *right;
} UnionType;

typedef struct GenericTypeDefinition {
    // T
    Type self;
    Type *extends;
} GenericTypeDefinition;

typedef struct InterfaceType {
    // {
    //    var a: Number
    //    add(other: Number)
    // }
    Type self;
    Table fields;
    Table methods;
    ValueArray genericArgs;
    Type *superType;
} InterfaceType;

// Todo: Maybe some sort of type for intrinsic simple types
// Or maybe just recursive compare all children and provide more complex
// types for builtins / primitives.

// Should 2 type equivalent classes be comparable? Probably not on type alone.
// Also todo: Inheritance, this, super, class attributes, instance attributes, generic types

typedef struct TypeEnvironment {
    struct TypeEnvironment *enclosing;
    FunctionType type;
    Table locals;
    Table typeDefs;
    ValueTable genericResolutions;
    int scopeDepth;
} TypeEnvironment;

SimpleType *newSimpleType();

FunctorType *newFunctorType();

UnionType *newUnionType();

InterfaceType *newInterfaceType();

GenericType *newGenericType();

SimpleType *numberType;
SimpleType *anyType;
SimpleType *boolType;
SimpleType *nilType;
SimpleType *atomType;
SimpleType *stringType;
SimpleType *neverType;
SimpleType *listTypeDef;
SimpleType *mapTypeDef;

void makeTypes();

void freeType(Type *type);

void markTypecheckerRoots();

#endif //CRAFTING_INTERPRETERS_TYPES_H
