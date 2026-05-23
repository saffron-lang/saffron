#ifndef saffron_vm_h
#define saffron_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"
#include "ast/ast.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum {
    AWAITED = 0,
    SPAWNED = 1,
    INITIATED = 2,
    FINISHED = 4,
} CallState;

typedef enum {
    MAIN = 0,
    IMPORT = 1,
} ModuleContext;

typedef struct ObjCallFrame {
    Obj obj;
    ObjClosure *closure;
    uint8_t *ip;
    int index;
    Value *slots;
    struct ObjCallFrame *parent;
    CallState state;

    Value stored;
    ValueArray stack;
    Value result;
} ObjCallFrame;

ObjCallFrame *currentFrame;

#define CURRENT_TASK \
    AS_CALL_FRAME(vm.tasks.values[vm.currentTask])

#define HANDLER_MAX 64

typedef struct {
    uint8_t *catchIp;
    Value *stackTop;
    ObjCallFrame *frame;
} ExceptionHandler;

typedef struct {
    ValueArray tasks;
    int currentTask;

    Value stack[STACK_MAX];
    Value *stackTop;
    Obj *objects;

    ExceptionHandler handlers[HANDLER_MAX];
    int handlerCount;
    Value currentException;
    bool isThrowing;

    int grayCount;
    int grayCapacity;
    Obj **grayStack;

    size_t bytesAllocated;
    size_t nextGC;
    bool vmReady;

    Table types;
    Table modules;
    Table builtins;
    Table strings;
    Table atoms;
    ObjString *initString;
    ObjClass *numberClass;
    ObjClass *stringClass;
    ObjClass *boolClass;
    ObjClass *nilClass;
    ObjString *addString;
    ObjString *subString;
    ObjString *mulString;
    ObjString *divString;
    ObjString *modString;
    ObjString *ltString;
    ObjString *gtString;
    ObjString *eqString;
    ObjString *getItemString;
    ObjUpvalue *openUpvalues;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
    ObjInstance obj;
    ObjString *name;
    ObjString *path;
    InterpretResult result;
} ObjModule;

extern VM vm;

void initVM();

void freeVM();

ObjModule *interpret(StmtArray *body, const char *name, const char *path);
InterpretResult interpretInModule(StmtArray *body, ObjModule *module);

void push(Value value);

Value pop();

Value peek(int distance);

void defineNative(const char *name, NativeFn function);

void defineGlobal(const char *name, Value value);

void defineModule(const char *name, Value value);

void defineType(const char *name, Value value);

void defineBuiltin(const char *name, Value value);

bool runtimeError(const char *format, ...);

void load_new_frame();

#endif