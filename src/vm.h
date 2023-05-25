#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum {
    AWAITED = 0,
    SPAWNED = 1,
    INITIATED = 2,
    FINISHED = 4,
} CallState;

typedef struct ObjCallFrame {
    Obj obj;
    ObjClosure* closure;
    uint8_t* ip;
    int index;
    Value* slots;
    struct ObjCallFrame* parent;
    CallState state;

    Value stored;
    ValueArray stack;
    Value result;
} ObjCallFrame;

ObjCallFrame *currentFrame;

#define CURRENT_TASK \
    AS_CALL_FRAME(vm.tasks.values[vm.currentTask])

typedef struct {
    ValueArray tasks;
    int currentTask;

    Value stack[STACK_MAX];
    Value* stackTop;
    Obj* objects;

    int grayCount;
    int grayCapacity;
    Obj** grayStack;
    size_t bytesAllocated;
    size_t nextGC;

    Table globals;
    Table builtins;
    Table strings;
    ObjString* initString;
    ObjUpvalue* openUpvalues;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

void push(Value value);
Value pop();
Value peek(int distance);

void defineNative(const char *name, NativeFn function);
void defineGlobal(const char *name, Value value);
void defineBuiltin(const char *name, Value value);
void runtimeError(const char *format, ...);
void load_new_frame();

#endif