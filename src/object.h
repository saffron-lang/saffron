#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_STRING(value)       isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)

#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CALL_FRAME(value) ((ObjCallFrame*)AS_OBJ(value))

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

typedef enum {
    OBJ_STRING,
    OBJ_ATOM,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_NATIVE_METHOD,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CLASS,
    OBJ_BUILTIN_TYPE,
    OBJ_PARSE_TYPE,
    OBJ_PARSE_FUNCTOR_TYPE,
    OBJ_PARSE_GENERIC_TYPE,
    OBJ_PARSE_GENERIC_DEFINITION_TYPE,
    OBJ_PARSE_UNION_TYPE,
    OBJ_PARSE_INTERFACE_TYPE,
    OBJ_INSTANCE,
    OBJ_LIST,
    OBJ_MAP,
    OBJ_BOUND_METHOD,
    OBJ_CALL_FRAME,
    OBJ_MODULE,
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj *next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char *chars;
    uint32_t hash;
};

typedef struct {
    ObjString obj;
} ObjAtom;

typedef struct ObjUpvalue {
    Obj obj;
    Value *location;
    struct ObjUpvalue *next;
    Value closed;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString *name;
    Table methods;
    Table fields;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

ObjFunction *newFunction();

ObjInstance *newInstance(ObjClass *klass);

ObjBoundMethod *newBoundMethod(Value receiver,
                               ObjClosure *method);

ObjClosure *newClosure(ObjFunction *function);

ObjNative *newNative(NativeFn function);

ObjClass *newClass(ObjString *name);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjString *copyString(const char *chars, int length);
ObjAtom *copyAtom(const char *chars, int length);

ObjUpvalue *newUpvalue(Value *slot);

void printObject(Value value);

ObjString *takeString(char *chars, int length);

Obj *allocateObject(size_t size, ObjType type);

#endif