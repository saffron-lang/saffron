#include "common.h"
#include "vm.h"
#include "debug.h"
#include "ast/astcompile.h"
#include "object.h"
#include "memory.h"
#include <math.h>
#include "libc/time.h"
#include "libc/list.h"
#include "libc/io.h"
#include "libc/async.h"
#include "libc/module.h"
#include "libc/task.h"
#include "files.h"
#include "ast/astparse.h"
#include "libc/map.h"
#include "libc/builtins.h"
#include "libc/string.h"
#include "libc/type.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

VM vm;

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void resetStack() {
    vm.stackTop = vm.stack;
    initValueArray(&vm.tasks);
}

void defineNative(const char *name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.builtins, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

void defineType(const char *name, Value value) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(value);
    tableSet(&vm.types, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

void defineModule(const char *name, Value value) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(value);
    tableSet(&vm.modules, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

void defineBuiltin(const char *name, Value value) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(value);
    tableSet(&vm.builtins, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

// Number native methods — receiver is Value* cast to Obj*
#define NUM_SELF(obj) AS_NUMBER(*(Value*)(obj))

static Value numberAbs(Obj *self, int argCount, Value *args) {
    double n = NUM_SELF(self);
    return NUMBER_VAL(n < 0 ? -n : n);
}

static Value numberFloor(Obj *self, int argCount, Value *args) {
    return NUMBER_VAL(floor(NUM_SELF(self)));
}

static Value numberCeil(Obj *self, int argCount, Value *args) {
    return NUMBER_VAL(ceil(NUM_SELF(self)));
}

static Value numberRound(Obj *self, int argCount, Value *args) {
    double n = NUM_SELF(self);
    if (argCount >= 1 && IS_NUMBER(args[0])) {
        int places = (int) AS_NUMBER(args[0]);
        double factor = pow(10, places);
        return NUMBER_VAL(round(n * factor) / factor);
    }
    return NUMBER_VAL(round(n));
}

static Value numberToString(Obj *self, int argCount, Value *args) {
    double n = NUM_SELF(self);
    char buf[64];
    if (n == (int)n) {
        snprintf(buf, sizeof(buf), "%d", (int)n);
    } else {
        snprintf(buf, sizeof(buf), "%g", n);
    }
    return OBJ_VAL(copyString(buf, (int)strlen(buf)));
}

// Bool native methods
#define BOOL_SELF(obj) AS_BOOL(*(Value*)(obj))

static Value boolToString(Obj *self, int argCount, Value *args) {
    bool b = BOOL_SELF(self);
    return b ? OBJ_VAL(copyString("true", 4)) : OBJ_VAL(copyString("false", 5));
}

static void defineMethodOnClass(ObjClass *klass, const char *name, NativeMethodFn fn) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNativeMethod(fn)));
    tableSet(&klass->methods, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.vmReady = false;

    vm.currentTask = 0;
    vm.objects = NULL;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024 * 1024; // 1GB - disable GC for self-compilation

    initTable(&vm.types);
    initTable(&vm.modules);
    initTable(&vm.builtins);
    initTable(&vm.strings);

    vm.initString = NULL;
    vm.initString = copyString("init", 4);
    vm.addString = copyString("add", 3);
    vm.subString = copyString("sub", 3);
    vm.mulString = copyString("mul", 3);
    vm.divString = copyString("div", 3);
    vm.modString = copyString("mod", 3);
    vm.ltString = copyString("lt", 2);
    vm.gtString = copyString("gt", 2);
    vm.eqString = copyString("eq", 2);
    vm.getItemString = copyString("getItem", 7);
    vm.setItemString = copyString("setItem", 7);
    vm.openUpvalues = NULL;

    vm.handlerCount = 0;
    vm.isThrowing = false;
    vm.currentException = NIL_VAL;

    // Push names onto stack to protect from GC during class allocation
    push(OBJ_VAL(copyString("Number", 6)));
    vm.numberClass = newClass(AS_STRING(peek(0)));
    pop();
    push(OBJ_VAL(copyString("String", 6)));
    vm.stringClass = newClass(AS_STRING(peek(0)));
    pop();
    push(OBJ_VAL(copyString("Bool", 4)));
    vm.boolClass = newClass(AS_STRING(peek(0)));
    pop();
    push(OBJ_VAL(copyString("Nil", 3)));
    vm.nilClass = newClass(AS_STRING(peek(0)));
    pop();

    defineBuiltin("Number", OBJ_VAL(vm.numberClass));
    defineBuiltin("String", OBJ_VAL(vm.stringClass));
    defineBuiltin("Bool", OBJ_VAL(vm.boolClass));
    defineBuiltin("Nil", OBJ_VAL(vm.nilClass));

    defineMethodOnClass(vm.numberClass, "abs", (NativeMethodFn) numberAbs);
    defineMethodOnClass(vm.numberClass, "floor", (NativeMethodFn) numberFloor);
    defineMethodOnClass(vm.numberClass, "ceil", (NativeMethodFn) numberCeil);
    defineMethodOnClass(vm.numberClass, "round", (NativeMethodFn) numberRound);
    defineMethodOnClass(vm.numberClass, "to_string", (NativeMethodFn) numberToString);
    defineMethodOnClass(vm.boolClass, "to_string", (NativeMethodFn) boolToString);

    makeTypes();
    initLib();
    initStringMethods();
    initAsyncHandler();
}

void freeVM() {
    freeAsyncHandler();
    freeStringMethods();

    freeTable(&vm.types);
    freeTable(&vm.modules);
    freeTable(&vm.builtins);
    freeTable(&vm.strings);
    vm.initString = NULL;
    freeNodes();
    freeObjects();
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static void concatenate() {
    ObjString *b = AS_STRING(peek(0));
    ObjString *a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

bool runtimeError(const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (vm.handlerCount > 0 && vm.vmReady) {
        ObjString *errorStr = copyString(message, (int) strlen(message));
        vm.currentException = OBJ_VAL(errorStr);
        vm.isThrowing = true;

        ExceptionHandler *handler = &vm.handlers[--vm.handlerCount];
        vm.stackTop = handler->stackTop;
        currentFrame = handler->frame;
        vm.tasks.values[vm.currentTask] = OBJ_VAL(currentFrame);
        currentFrame->ip = handler->catchIp;
        push(OBJ_VAL(errorStr));
        vm.isThrowing = false;
        return true; // error was caught
    }

    fprintf(stderr, "%s\n", message);

    for (ObjCallFrame *frame = CURRENT_TASK; frame->parent != NULL; frame = frame->parent) {
        ObjFunction *function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        int line = getLine(&function->chunk, (int)instruction);
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
    return false; // fatal
}

ObjModule *executeModule(ObjString *name, const char *importingPath);

static bool call(ObjClosure *closure, int argCount) {
    switch (closure->obj.type) {
        case OBJ_CLOSURE: {
            if (closure->function->hasRest) {
                int minArgs = closure->function->arity - 1;
                if (argCount < minArgs) {
                    runtimeError("Expected at least %d arguments but got %d.",
                                 minArgs, argCount);
                    return false;
                }
            } else if (argCount != closure->function->arity) {
                runtimeError("Expected %d arguments but got %d.",
                             closure->function->arity, argCount);
                return false;
            }

            if (vm.tasks.count == FRAMES_MAX) {
                runtimeError("Stack overflow.");
                return false;
            }

            ObjCallFrame *frame = ALLOCATE_OBJ(ObjCallFrame, OBJ_CALL_FRAME);
            frame->closure = closure;
            frame->ip = closure->function->chunk.code;
            frame->slots = vm.stackTop - argCount - 1;
            frame->state = AWAITED | INITIATED;
            frame->stored = NIL_VAL;

            initValueArray(&frame->stack);
            frame->result = NIL_VAL;

            if (vm.tasks.count == 0) {
                frame->parent = NULL;
                frame->index = 0;
                writeValueArray(&vm.tasks, OBJ_VAL(frame));
            } else {
                frame->parent = CURRENT_TASK;
                frame->index = frame->parent->index + 1;
                vm.tasks.values[vm.currentTask] = OBJ_VAL(frame);
            }

            return true;
        }
        case OBJ_NATIVE_METHOD: {
            ObjNativeMethod *nativeMethod = (ObjNativeMethod *) closure;
            NativeMethodFn native = nativeMethod->function;
            Value result = native(AS_OBJ(peek(argCount)), argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(result);
            return true;
        }
        default:
            runtimeError("Invalid call target.");
            return false;
    }
    return false;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);

                return true;
            }
            case OBJ_BUILTIN_TYPE: {
                ObjBuiltinType *type = AS_BUILTIN_TYPE(callee);
                Value result = type->typeCallFn(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            case OBJ_CLASS: {
                ObjClass *klass = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));

                Value initializer;
                if (tableGet(&klass->methods, vm.initString,
                             &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.",
                                 argCount);
                    return false;
                }

                return true;
            }
            case OBJ_OVERLOAD_SET: {
                ObjOverloadSet *set = AS_OVERLOAD_SET(callee);
                ObjClosure *match = NULL;
                ObjClosure *anyMatch = NULL;

                for (int i = 0; i < set->count; i++) {
                    if (set->arities[i] != argCount) continue;

                    OverloadParamType expected = set->firstParamTypes[i];
                    if (expected == OVERLOAD_ANY) {
                        if (anyMatch == NULL) anyMatch = set->closures[i];
                        continue;
                    }

                    // Check if first arg matches expected type
                    Value firstArg = peek(argCount - 1);
                    bool matches = false;
                    switch (expected) {
                        case OVERLOAD_NUMBER: matches = IS_NUMBER(firstArg); break;
                        case OVERLOAD_STRING: matches = IS_STRING(firstArg); break;
                        case OVERLOAD_BOOL: matches = IS_BOOL(firstArg); break;
                        case OVERLOAD_NIL: matches = IS_NIL(firstArg); break;
                        case OVERLOAD_LIST: matches = IS_OBJ(firstArg) && AS_OBJ(firstArg)->type == OBJ_LIST; break;
                        case OVERLOAD_MAP: matches = IS_OBJ(firstArg) && AS_OBJ(firstArg)->type == OBJ_MAP; break;
                        case OVERLOAD_INSTANCE: matches = IS_INSTANCE(firstArg); break;
                        default: break;
                    }

                    if (matches) {
                        match = set->closures[i];
                        break;
                    }
                }

                if (match == NULL) match = anyMatch;
                if (match != NULL) return call(match, argCount);
                runtimeError("No overload matches the given arguments.");
                return false;
            }
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bound = AS_BOUND_METHOD(callee);

                switch (bound->method->obj.type) {
                    case OBJ_CLOSURE:
                        vm.stackTop[-argCount - 1] = bound->receiver;
                        return call(bound->method, argCount);
                    case OBJ_NATIVE_METHOD:
                        vm.stackTop[-argCount - 1] = bound->receiver;
                        NativeMethodFn native = ((ObjNativeMethod *) bound->method)->function;
                        Value result = native(AS_OBJ(bound->receiver), argCount, vm.stackTop - argCount);
                        vm.stackTop -= argCount + 1;
                        push(result);
                        return true;
                    case OBJ_OVERLOAD_SET: {
                        vm.stackTop[-argCount - 1] = bound->receiver;
                        ObjOverloadSet *set = (ObjOverloadSet *) bound->method;
                        ObjClosure *match = NULL;
                        ObjClosure *anyMatch = NULL;

                        for (int i = 0; i < set->count; i++) {
                            if (set->arities[i] != argCount) continue;

                            OverloadParamType expected = set->firstParamTypes[i];
                            if (expected == OVERLOAD_ANY) {
                                if (anyMatch == NULL) anyMatch = set->closures[i];
                                continue;
                            }

                            Value firstArg = peek(argCount - 1);
                            bool matches = false;
                            switch (expected) {
                                case OVERLOAD_NUMBER: matches = IS_NUMBER(firstArg); break;
                                case OVERLOAD_STRING: matches = IS_STRING(firstArg); break;
                                case OVERLOAD_BOOL: matches = IS_BOOL(firstArg); break;
                                case OVERLOAD_NIL: matches = IS_NIL(firstArg); break;
                                case OVERLOAD_LIST: matches = IS_OBJ(firstArg) && AS_OBJ(firstArg)->type == OBJ_LIST; break;
                                case OVERLOAD_MAP: matches = IS_OBJ(firstArg) && AS_OBJ(firstArg)->type == OBJ_MAP; break;
                                case OVERLOAD_INSTANCE: matches = IS_INSTANCE(firstArg); break;
                                default: break;
                            }

                            if (matches) {
                                match = set->closures[i];
                                break;
                            }
                        }

                        if (match == NULL) match = anyMatch;
                        if (match != NULL) return call(match, argCount);
                        runtimeError("No overload matches the given arguments.");
                        return false;
                    }
                    default: break;
                }
                break;
            }
            default:
                break; // Non-callable object type.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

static ObjUpvalue *captureUpvalue(Value *local) {
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value *last) {
    while (vm.openUpvalues != NULL &&
           vm.openUpvalues->location >= last) {
        ObjUpvalue *upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString *name) {
    Value method = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));

    Value existing;
    if (tableGet(&klass->methods, name, &existing)) {
        ObjOverloadSet *set;
        if (IS_OBJ(existing) && AS_OBJ(existing)->type == OBJ_OVERLOAD_SET) {
            set = AS_OVERLOAD_SET(existing);
        } else {
            set = newOverloadSet();
            push(OBJ_VAL(set));
            ObjClosure *existingClosure = AS_CLOSURE(existing);
            addOverload(set, existingClosure, existingClosure->function->arity,
                        existingClosure->function->firstParamType);
            pop();
        }
        ObjClosure *closure = AS_CLOSURE(method);
        addOverload(set, closure, closure->function->arity, closure->function->firstParamType);
        tableSet(&klass->methods, name, OBJ_VAL(set));
    } else {
        tableSet(&klass->methods, name, method);
    }
    pop();
}

static void defineField(ObjString *name) {
    Value method = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));
    tableSet(&klass->fields, name, method);
    pop();
}

static bool bindMethod(ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        printValue(peek(0));
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    if (IS_OBJ(method) && AS_OBJ(method)->type == OBJ_OVERLOAD_SET) {
        ObjBoundMethod *bound = newBoundMethod(peek(0), (ObjClosure *) AS_OBJ(method));
        pop();
        push(OBJ_VAL(bound));
        return true;
    }

    ObjBoundMethod *bound = newBoundMethod(peek(0),
                                           AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static bool invokeFromClass(ObjClass *klass, ObjString *name,
                            int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    if (IS_OBJ(method) && AS_OBJ(method)->type == OBJ_OVERLOAD_SET) {
        ObjOverloadSet *set = AS_OVERLOAD_SET(method);
        ObjClosure *match = NULL;
        ObjClosure *anyMatch = NULL;

        for (int i = 0; i < set->count; i++) {
            if (set->arities[i] != argCount) continue;

            OverloadParamType expected = set->firstParamTypes[i];
            if (expected == OVERLOAD_ANY) {
                if (anyMatch == NULL) anyMatch = set->closures[i];
                continue;
            }

            Value firstArg = peek(argCount - 1);
            bool matches = false;
            switch (expected) {
                case OVERLOAD_NUMBER: matches = IS_NUMBER(firstArg); break;
                case OVERLOAD_STRING: matches = IS_STRING(firstArg); break;
                case OVERLOAD_BOOL: matches = IS_BOOL(firstArg); break;
                case OVERLOAD_NIL: matches = IS_NIL(firstArg); break;
                case OVERLOAD_LIST: matches = IS_OBJ(firstArg) && AS_OBJ(firstArg)->type == OBJ_LIST; break;
                case OVERLOAD_MAP: matches = IS_OBJ(firstArg) && AS_OBJ(firstArg)->type == OBJ_MAP; break;
                case OVERLOAD_INSTANCE: matches = IS_INSTANCE(firstArg); break;
                default: break;
            }

            if (matches) {
                match = set->closures[i];
                break;
            }
        }

        if (match == NULL) match = anyMatch;
        if (match != NULL) return call(match, argCount);
        runtimeError("No overload matches the given arguments.");
        return false;
    }

    return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString *name, int argCount) {
    Value receiver = peek(argCount);

    if (IS_CLASS(receiver)) {
        ObjClass *klass = AS_CLASS(receiver);
        Value value;
        if (tableGet(&klass->methods, name, &value)) {
            vm.stackTop[-argCount - 1] = value;
            return callValue(value, argCount);
        }
        runtimeError("Undefined method '%s' on class.", name->chars);
        return false;
    }

    if (IS_STRING(receiver)) {
        Value method;
        if (tableGet(&stringMethods, name, &method)) {
            ObjNativeMethod *nativeMethod = (ObjNativeMethod *) AS_OBJ(method);
            NativeMethodFn native = nativeMethod->function;
            Value result = native(AS_OBJ(receiver), argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(result);
            return true;
        }
        runtimeError("Undefined method '%s' on String.", name->chars);
        return false;
    }

    if (IS_NUMBER(receiver)) {
        Value method;
        if (tableGet(&vm.numberClass->methods, name, &method)) {
            ObjNativeMethod *nativeMethod = (ObjNativeMethod *) AS_OBJ(method);
            NativeMethodFn native = nativeMethod->function;
            // Pass pointer to receiver slot as "self" — native casts back to Value*
            Value result = native((Obj *)(vm.stackTop - argCount - 1), argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(result);
            return true;
        }
        runtimeError("Undefined method '%s' on Number.", name->chars);
        return false;
    }

    if (IS_BOOL(receiver)) {
        Value method;
        if (tableGet(&vm.boolClass->methods, name, &method)) {
            ObjNativeMethod *nativeMethod = (ObjNativeMethod *) AS_OBJ(method);
            NativeMethodFn native = nativeMethod->function;
            Value result = native((Obj *)(vm.stackTop - argCount - 1), argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(result);
            return true;
        }
        runtimeError("Undefined method '%s' on Bool.", name->chars);
        return false;
    }

    if (IS_NIL(receiver)) {
        runtimeError("Cannot call method '%s' on nil.", name->chars);
        return false;
    }

    if (!(IS_INSTANCE(receiver) || IS_LIST(receiver) || IS_MAP(receiver))) {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance *instance = AS_INSTANCE(receiver);

    Value value;
    bool foundField = tableGet(&instance->fields, name, &value);
    if (foundField) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

ObjCallFrame *currentFrame;

static void save_current_frame() {
    freeValueArray(&currentFrame->stack);
    Value *stackBottom = vm.stack;
//    printf("Saving into: ");
//    printValue(OBJ_VAL(currentFrame));
//    printf("\n");

    while (stackBottom < vm.stackTop) {
//        printf("Saving value: ");
//        printValue(*stackBottom);
//        printf("\n");
        writeValueArray(&currentFrame->stack, *stackBottom);
        stackBottom++;
    }
}


void load_new_frame() {
    vm.stackTop = vm.stack;

//    printf("Loading from: ");
//    printValue(OBJ_VAL(CURRENT_TASK));
//    printf("\n");
    for (int i = 0; i < CURRENT_TASK->stack.count; i++) {
//        printf("Loading value: ");
//        printValue(CURRENT_TASK->stack.values[i]);
//        printf("\n");
        push(CURRENT_TASK->stack.values[i]);
    }

    freeValueArray(&CURRENT_TASK->stack);

    // So the yield evaluates to an expression before popping
    if (CURRENT_TASK->state & INITIATED) {
        push(CURRENT_TASK->stored);
    } else {
        CURRENT_TASK->state |= INITIATED;
    }

    currentFrame = CURRENT_TASK;
}

static void pop_frame() {
    popValueArray(&vm.tasks, vm.currentTask);
    if (vm.currentTask >= vm.tasks.count) {
        getTasks();
    }
    vm.currentTask = vm.tasks.count ? vm.currentTask % vm.tasks.count : 0;

    if (CURRENT_TASK) {
        load_new_frame();
    }
}

static void POP_CALL(Value result) {
    CURRENT_TASK->result = result;
    if (CURRENT_TASK->state & SPAWNED) {
        pop_frame();
    } else {
        if (CURRENT_TASK->parent == NULL) {
            pop_frame();
        } else {
            vm.tasks.values[vm.currentTask] = OBJ_VAL(CURRENT_TASK->parent);
            vm.stackTop = currentFrame->slots;
            push(result);
        }
    }

    currentFrame = CURRENT_TASK;
}

ModuleContext moduleContext = MAIN;

static InterpretResult run(ObjModule *module) {
    currentFrame = CURRENT_TASK;

#define READ_BYTE() (*currentFrame->ip++)

#define READ_SHORT() \
    (currentFrame->ip += 2, \
    (uint16_t)((currentFrame->ip[-2] << 8) | currentFrame->ip[-1]))

#define READ_CONSTANT() \
    (currentFrame->closure->function->chunk.constants.values[READ_SHORT()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        if (!runtimeError("Operands must be numbers for binary op.")) \
            return INTERPRET_RUNTIME_ERROR; \
        goto dispatch; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");

        disassembleInstruction(&currentFrame->closure->function->chunk,
                               (int) (currentFrame->ip - currentFrame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        dispatch:
        switch (instruction = READ_BYTE()) {
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    if (!runtimeError("Runtime error.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.addString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else if (IS_STRING(peek(0)) || IS_STRING(peek(1))) {
                    // Keep both operands on the stack during allocation
                    // to protect them from GC.
                    char aBuf[2048], bBuf[2048];
                    int bLen = sprintValue(bBuf, sizeof(bBuf), peek(0));
                    int aLen = sprintValue(aBuf, sizeof(aBuf), peek(1));
                    char *buf = ALLOCATE(char, aLen + bLen + 1);
                    memcpy(buf, aBuf, aLen);
                    memcpy(buf + aLen, bBuf, bLen);
                    buf[aLen + bLen] = '\0';
                    ObjString *result = takeString(buf, aLen + bLen);
                    pop();
                    pop();
                    push(OBJ_VAL(result));
                } else {
                    if (!runtimeError(
                            "Operands must be two numbers or two strings.")) return INTERPRET_RUNTIME_ERROR;
                    goto dispatch;
                }
                break;
            }
            case OP_MODULO: {
                if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(fmod(a, b)));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.modString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else {
                    if (!runtimeError("Operands not supported for '%%'.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }
                break;
            }
            case OP_SUBTRACT: {
                if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a - b));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.subString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else {
                    BINARY_OP(NUMBER_VAL, -);
                }
                break;
            }
            case OP_MULTIPLY: {
                if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a * b));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.mulString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else {
                    BINARY_OP(NUMBER_VAL, *);
                }
                break;
            }
            case OP_DIVIDE: {
                if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a / b));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.divString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else {
                    BINARY_OP(NUMBER_VAL, /);
                }
                break;
            }
            case OP_BITWISE_AND: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((double)((long long)a & (long long)b)));
                break;
            }
            case OP_BITWISE_OR: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((double)((long long)a | (long long)b)));
                break;
            }
            case OP_BITWISE_XOR: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((double)((long long)a ^ (long long)b)));
                break;
            }
            case OP_BITWISE_NOT: {
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((double)(~(long long)a)));
                break;
            }
            case OP_SHIFT_LEFT: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((double)((long long)a << (long long)b)));
                break;
            }
            case OP_SHIFT_RIGHT: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((double)((long long)a >> (long long)b)));
                break;
            }
            case OP_NIL:
                push(NIL_VAL);
                break;
            case OP_TRUE:
                push(BOOL_VAL(true));
                break;
            case OP_FALSE:
                push(BOOL_VAL(false));
                break;
            case OP_GREATER: {
                if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(BOOL_VAL(a > b));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.gtString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else {
                    BINARY_OP(BOOL_VAL, >);
                }
                break;
            }
            case OP_LESS: {
                if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(BOOL_VAL(a < b));
                } else if (IS_INSTANCE(peek(1))) {
                    if (!invoke(vm.ltString, 1)) return INTERPRET_RUNTIME_ERROR;
                    currentFrame = CURRENT_TASK;
                } else {
                    BINARY_OP(BOOL_VAL, <);
                }
                break;
            }
            case OP_EQUAL: {
                if (IS_INSTANCE(peek(1))) {
                    Value method;
                    ObjInstance *inst = AS_INSTANCE(peek(1));
                    if (tableGet(&inst->klass->methods, vm.eqString, &method)) {
                        if (!invoke(vm.eqString, 1)) return INTERPRET_RUNTIME_ERROR;
                        currentFrame = CURRENT_TASK;
                        break;
                    }
                }
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_IS: {
                Value typeVal = pop();
                Value instance = pop();
                bool result = false;
                if (IS_CLASS(typeVal) || isObjType(typeVal, OBJ_BUILTIN_TYPE)) {
                    ObjClass *target = AS_CLASS(typeVal);
                    // Primitive type checks
                    if (IS_NUMBER(instance) && target == vm.numberClass) {
                        result = true;
                    } else if (IS_STRING(instance) && target == vm.stringClass) {
                        result = true;
                    } else if (IS_BOOL(instance) && target == vm.boolClass) {
                        result = true;
                    } else if (IS_NIL(instance) && target == vm.nilClass) {
                        result = true;
                    } else if (IS_OBJ(instance)) {
                        Obj *obj = AS_OBJ(instance);
                        ObjClass *klass = NULL;
                        if (obj->type == OBJ_INSTANCE) {
                            klass = ((ObjInstance *)obj)->klass;
                        } else if (obj->type == OBJ_LIST) {
                            klass = ((ObjInstance *)obj)->klass;
                        } else if (obj->type == OBJ_MAP) {
                            klass = ((ObjInstance *)obj)->klass;
                        }
                        if (klass != NULL) {
                            ObjClass *stack[64];
                            int stackSize = 0;
                            stack[stackSize++] = klass;
                            while (stackSize > 0 && !result) {
                                ObjClass *current = stack[--stackSize];
                                if (current == target) {
                                    result = true;
                                } else {
                                    for (int i = 0; i < current->superclassCount && stackSize < 64; i++) {
                                        stack[stackSize++] = current->superclasses[i];
                                    }
                                }
                            }
                        }
                    }
                }
                push(BOOL_VAL(result));
                break;
            }
            case OP_POP:
                pop();
                break;
            case OP_DUP:
                push(peek(0));
                break;
            case OP_DEFINE_GLOBAL: {
                ObjString *name = READ_STRING();
                ObjModule *targetModule = currentFrame->closure->function->module
                    ? (ObjModule *)currentFrame->closure->function->module : module;
                Value existing;
                if (IS_CLOSURE(peek(0)) && tableGet(&targetModule->obj.fields, name, &existing)) {
                    if (IS_CLOSURE(existing)) {
                        ObjOverloadSet *set = newOverloadSet();
                        addOverload(set, AS_CLOSURE(existing), AS_CLOSURE(existing)->function->arity,
                                    AS_CLOSURE(existing)->function->firstParamType);
                        addOverload(set, AS_CLOSURE(peek(0)), AS_CLOSURE(peek(0))->function->arity,
                                    AS_CLOSURE(peek(0))->function->firstParamType);
                        tableSet(&targetModule->obj.fields, name, OBJ_VAL(set));
                    } else if (IS_OVERLOAD_SET(existing)) {
                        ObjOverloadSet *set = AS_OVERLOAD_SET(existing);
                        addOverload(set, AS_CLOSURE(peek(0)), AS_CLOSURE(peek(0))->function->arity,
                                    AS_CLOSURE(peek(0))->function->firstParamType);
                    } else {
                        tableSet(&targetModule->obj.fields, name, peek(0));
                    }
                } else {
                    tableSet(&targetModule->obj.fields, name, peek(0));
                }
                pop();
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                ObjModule *targetModule = currentFrame->closure->function->module
                    ? (ObjModule *)currentFrame->closure->function->module : module;
                Value value;
                if (!tableGet(&targetModule->obj.fields, name, &value)) {
                    // Fall back to builtins
                    if (!tableGet(&vm.builtins, name, &value)) {
                        if (!runtimeError("Undefined variable '%s'.", name->chars)) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                    }
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                ObjModule *targetModule = currentFrame->closure->function->module
                    ? (ObjModule *)currentFrame->closure->function->module : module;
                if (tableSet(&targetModule->obj.fields, name, peek(0))) {
                    tableDelete(&targetModule->obj.fields, name);
                    if (!runtimeError("Undefined variable '%s'.", name->chars)) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(currentFrame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                currentFrame->slots[slot] = peek(0);
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                currentFrame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) currentFrame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                currentFrame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                VM *localVM = &vm;
                currentFrame = CURRENT_TASK;
                break;
            }
            case OP_GETITEM: {
                Value indexValue = pop();
                Value value = pop();
                if (isObjType(value, OBJ_LIST)) {
                    int index = trunc(AS_NUMBER(indexValue));
                    push(getListItem((ObjList *) AS_OBJ(value), index));
                } else if (isObjType(value, OBJ_MAP)) {
                    push(getMapItem((ObjMap *) AS_OBJ(value), indexValue));
                } else if (IS_ENUM_INSTANCE(value)) {
                    int index = (int) AS_NUMBER(indexValue);
                    ObjEnumInstance *instance = AS_ENUM_INSTANCE(value);
                    if (index >= 0 && index < instance->fields.count) {
                        push(instance->fields.values[index]);
                    } else {
                        push(NIL_VAL);
                    }
                } else if (IS_INSTANCE(value)) {
                    // Operator overload: obj[key] dispatches to obj.getItem(key)
                    push(value);
                    push(indexValue);
                    if (!invoke(vm.getItemString, 1)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    currentFrame = CURRENT_TASK;
                } else {
                    runtimeError("Type does not support indexing.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SETITEM: {
                // Stack: [obj, index, value] (top)
                // Keep values on stack during potential GC-triggering operations.
                Value value = peek(0);
                Value indexValue = peek(1);
                Value obj = peek(2);
                if (isObjType(obj, OBJ_LIST)) {
                    ObjList *list = (ObjList *) AS_OBJ(obj);
                    int index = (int) trunc(AS_NUMBER(indexValue));
                    if (index < 0) index += list->items.count;
                    if (index < 0 || index >= list->items.count) {
                        runtimeError("Index out of bounds.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    list->items.values[index] = value;
                    pop(); pop(); pop();
                    push(value);
                } else if (isObjType(obj, OBJ_MAP)) {
                    ObjMap *map = (ObjMap *) AS_OBJ(obj);
                    // valueTableSet may trigger GC; all values are still on the stack.
                    valueTableSet(&map->values, indexValue, value);
                    pop(); pop(); pop();
                    push(value);
                } else if (IS_INSTANCE(obj)) {
                    // Operator overload: obj[key] = val dispatches to obj.setItem(key, val)
                    // Rearrange stack from [obj, index, value] to [obj, index, value]
                    // which is already the right order for invoke(setItem, 2).
                    pop(); pop(); pop();
                    push(obj);
                    push(indexValue);
                    push(value);
                    if (!invoke(vm.setItemString, 2)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    currentFrame = CURRENT_TASK;
                } else {
                    runtimeError("Type does not support indexed assignment.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_PIPE: {
                Value callee = pop();
                Value argument = pop();
                push(callee);
                push(argument);

                if (!callValue(peek(1), 1)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                currentFrame = CURRENT_TASK;
                break;
            }
            case OP_LIST: {
                int argCount = READ_BYTE();
                ObjList *list = newList();
                push(OBJ_VAL(list));
                for (int i = argCount; i > 0; i--) {
                    listPush(list, peek(i));
                }
                for (int i = 0; i < argCount + 1; i++) {
                    pop();
                }
                push(OBJ_VAL(list));
                break;
            }
            case OP_MAP: {
                int argCount = READ_BYTE();
                ObjMap *map = newMap();
                push(OBJ_VAL(map));
                for (int i = argCount; i > 0; i--) {
                    valueTableSet(&map->values, peek(2 * i), peek(2 * i - 1));
                }
                // Pop the 2*argCount key-value pairs plus the temp map reference
                for (int i = 0; i < argCount; i++) {
                    pop();
                    pop();
                }
                pop(); // the temp map
                push(OBJ_VAL(map));
                break;
            }
            case OP_CLOSURE: {
                ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
                // Inherit module from enclosing function
                if (function->module == NULL) {
                    function->module = currentFrame->closure->function->module;
                }
                ObjClosure *closure = newClosure(function);
                push(OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(currentFrame->slots + index);
                    } else {
                        closure->upvalues[i] = currentFrame->closure->upvalues[index];
                    }
                }

                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*currentFrame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *currentFrame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            case OP_GET_PROPERTY: {
                if (IS_CLASS(peek(0))) {
                    ObjClass *klass = AS_CLASS(peek(0));
                    ObjString *name = READ_STRING();
                    Value value;
                    if (tableGet(&klass->methods, name, &value)) {
                        pop();
                        push(value);
                        break;
                    }
                    if (!runtimeError("Runtime error.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }

                if (!IS_INSTANCE(peek(0)) && !IS_LIST(peek(0))) {
                    if (!runtimeError("Runtime error.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }

                ObjInstance *instance = AS_INSTANCE(peek(0));
                ObjString *name = READ_STRING();

                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(); // Instance.
                    push(value);
                    break;
                }

                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                ObjInstance *instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop();
                push(value);
                break;
            }
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
            case OP_FIELD:
                defineField(READ_STRING());
                break;
            case OP_FIELD_META: {
                ObjString *fieldName = READ_STRING();
                ObjString *typeName = READ_STRING();
                ObjClass *klass = AS_CLASS(peek(0));
                klass->isDataClass = true;

                FieldMeta meta;
                meta.name = fieldName;
                meta.typeName = typeName;
                if (memcmp(typeName->chars, "String", 6) == 0) meta.typeTag = FIELD_TYPE_STRING;
                else if (memcmp(typeName->chars, "Number", 6) == 0) meta.typeTag = FIELD_TYPE_NUMBER;
                else if (memcmp(typeName->chars, "Bool", 4) == 0) meta.typeTag = FIELD_TYPE_BOOL;
                else if (memcmp(typeName->chars, "Nil", 3) == 0) meta.typeTag = FIELD_TYPE_NIL;
                else if (memcmp(typeName->chars, "List", 4) == 0) meta.typeTag = FIELD_TYPE_LIST;
                else if (memcmp(typeName->chars, "Map", 3) == 0) meta.typeTag = FIELD_TYPE_MAP;
                else meta.typeTag = FIELD_TYPE_CLASS;

                if (klass->fieldMetas.count >= klass->fieldMetas.capacity) {
                    int oldCap = klass->fieldMetas.capacity;
                    klass->fieldMetas.capacity = oldCap < 8 ? 8 : oldCap * 2;
                    klass->fieldMetas.entries = GROW_ARRAY(FieldMeta, klass->fieldMetas.entries, oldCap, klass->fieldMetas.capacity);
                }
                klass->fieldMetas.entries[klass->fieldMetas.count++] = meta;
                break;
            }
            case OP_CLASS_DOC: {
                ObjString *doc = READ_STRING();
                ObjClass *klass = AS_CLASS(peek(0));
                klass->docstring = doc;
                break;
            }
            case OP_INVOKE: {
                ObjString *method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                currentFrame = CURRENT_TASK;
                break;
            }
            case OP_INHERIT: {
                Value superclass = peek(1);
                if (!IS_CLASS(superclass) && !IS_BUILTIN_TYPE(superclass)) {
                    if (!runtimeError("Runtime error.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }

                ObjClass *subclass = AS_CLASS(peek(0));
                tableAddAll(&AS_CLASS(superclass)->methods,
                            &subclass->methods);
                if (subclass->superclassCount < 8) {
                    subclass->superclasses[subclass->superclassCount++] = AS_CLASS(superclass);
                }
                pop(); // Subclass.
                break;
            }
            case OP_GET_SUPER: {
                ObjString *name = READ_STRING();
                ObjClass *superclass = AS_CLASS(pop());

                if (!bindMethod(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUPER_INVOKE: {
                ObjString *method = READ_STRING();
                int argCount = READ_BYTE();
                ObjClass *superclass = AS_CLASS(pop());
                if (!invokeFromClass(superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                currentFrame = CURRENT_TASK;
                break;
            }
            case OP_YIELD: {
                Value value = pop();
                // I don't remember why this second pop was here
                // Hope removing it doesn't mess anything up
                // pop();

                save_current_frame();
                handle_yield_value(value);

                if (vm.tasks.count) {
                    load_new_frame();
                } else {
                    int status;
                    while (true) {
                        status = getTasks();
                        switch (status) {
                            case 0: {
                                pop();
                                return INTERPRET_OK;
                            }
                            case -1: {
                                unsigned int utime = 10000;
                                usleep(utime);
                                // TODO: Sleep until a task is ready bit by bit
                                continue;
                            }
                            case 1: {
                                load_new_frame();
                                break;
                            }
                        }
                        break;
                    }
                }
                currentFrame = CURRENT_TASK;

                break;
            }
            case OP_IMPORT: {
                Value relPath = peek(0);
                ObjModule *newModule = executeModule(AS_STRING(relPath), module->path->chars);
                pop(); // pop the path string
                push(OBJ_VAL(newModule));
                break;
            }
            case OP_CONSTRUCT_VARIANT: {
                ObjString *tag = READ_STRING();
                ObjString *enumName = READ_STRING();
                int arity = READ_BYTE();
                ObjEnumInstance *instance = newEnumInstance(tag, enumName);
                push(OBJ_VAL(instance)); // protect from GC
                for (int i = arity; i > 0; i--) {
                    writeValueArray(&instance->fields, peek(i));
                }
                // pop instance + arity values, then push instance back
                pop(); // pop the temporary instance
                vm.stackTop -= arity;
                push(OBJ_VAL(instance));
                break;
            }
            case OP_GET_TAG: {
                Value value = peek(0);
                if (!IS_ENUM_INSTANCE(value)) {
                    if (!runtimeError("Runtime error.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }
                ObjEnumInstance *instance = AS_ENUM_INSTANCE(value);
                pop();
                push(OBJ_VAL(instance->tag));
                break;
            }
            case OP_SLICE: {
                int fromEnd = (int) AS_NUMBER(pop());
                int start = (int) AS_NUMBER(pop());
                // Keep the list on the stack to protect from GC during allocations.
                Value listVal = peek(0);
                if (!isObjType(listVal, OBJ_LIST)) {
                    pop();
                    if (!runtimeError("Runtime error.")) return INTERPRET_RUNTIME_ERROR; goto dispatch;
                }
                ObjList *source = (ObjList *) AS_OBJ(listVal);
                int end = source->items.count - fromEnd;
                ObjList *slice = newList();
                // Push slice; stack now has [source, slice]
                push(OBJ_VAL(slice));
                for (int i = start; i < end; i++) {
                    listPush(slice, source->items.values[i]);
                }
                // Remove source from under slice: pop slice, pop source, push slice
                Value sliceVal = pop();
                pop(); // pop source
                push(sliceVal);
                break;
            }
            case OP_RANGE: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                    if (!runtimeError("Range operands must be numbers.")) return INTERPRET_RUNTIME_ERROR;
                    goto dispatch;
                }
                double rangeEnd = AS_NUMBER(pop());
                double rangeStart = AS_NUMBER(pop());

                ObjList *rangeList = newList();
                push(OBJ_VAL(rangeList));

                if (rangeStart <= rangeEnd) {
                    for (int i = (int)rangeStart; i < (int)rangeEnd; i++) {
                        listPush(rangeList, NUMBER_VAL(i));
                    }
                } else {
                    for (int i = (int)rangeStart; i > (int)rangeEnd; i--) {
                        listPush(rangeList, NUMBER_VAL(i));
                    }
                }

                break;
            }
            case OP_ENUM:
            case OP_VARIANT:
            case OP_MATCH_TAG:
                break;
            case OP_THROW: {
                Value exception = pop();
                vm.currentException = exception;
                vm.isThrowing = true;

                if (vm.handlerCount > 0) {
                    ExceptionHandler *handler = &vm.handlers[--vm.handlerCount];
                    vm.stackTop = handler->stackTop;
                    currentFrame = handler->frame;
                    vm.tasks.values[vm.currentTask] = OBJ_VAL(currentFrame);
                    currentFrame->ip = handler->catchIp;
                    push(exception);
                    vm.isThrowing = false;
                } else {
                    fprintf(stderr, "Unhandled exception: ");
                    printValue(exception);
                    fprintf(stderr, "\n");
                    for (ObjCallFrame *frame = CURRENT_TASK; frame->parent != NULL; frame = frame->parent) {
                        ObjFunction *function = frame->closure->function;
                        size_t instruction = frame->ip - function->chunk.code - 1;
                        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
                        if (function->name == NULL) fprintf(stderr, "script\n");
                        else fprintf(stderr, "%s()\n", function->name->chars);
                    }
                    vm.isThrowing = false;
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_TRY_BEGIN: {
                uint16_t offset = READ_SHORT();
                ExceptionHandler *handler = &vm.handlers[vm.handlerCount++];
                handler->catchIp = currentFrame->ip + offset;
                handler->stackTop = vm.stackTop;
                handler->frame = currentFrame;
                break;
            }
            case OP_TRY_END: {
                vm.handlerCount--;
                break;
            }
            case OP_PACK_REST: {
                int restIndex = READ_BYTE();
                // slots points to the start of this call frame's locals
                // slot[0] = function itself, slot[1..] = args
                // restIndex is the parameter index (0-based) where rest starts
                // The actual arg slot is restIndex + 1 (slot 0 is the fn)
                Value *restSlot = currentFrame->slots + restIndex + 1;
                int totalArgs = (int)(vm.stackTop - currentFrame->slots) - 1;
                int restCount = totalArgs - restIndex;

                ObjList *list = newList();
                push(OBJ_VAL(list)); // protect from GC

                if (restCount > 0) {
                    for (int i = 0; i < restCount; i++) {
                        writeValueArray(&list->items, restSlot[i]);
                    }
                }

                pop(); // unprotect
                // Collapse: put list at restSlot, move stack top back
                *restSlot = OBJ_VAL(list);
                vm.stackTop = restSlot + 1;
                break;
            }
            case OP_CALL_SPREAD:
                break;
            case OP_RETURN: {
                Value result = pop();
                currentFrame->state |= FINISHED;
                closeUpvalues(currentFrame->slots);

                if (currentFrame->closure->function->name == NULL && moduleContext == IMPORT) {
                    vm.tasks.values[vm.currentTask] = OBJ_VAL(CURRENT_TASK->parent);
                    vm.stackTop = currentFrame->slots;
                    push(result);
                    currentFrame = CURRENT_TASK;
                    pop();
                    return INTERPRET_OK;
                }

                POP_CALL(result);
                if (currentFrame == NULL) {
                    int status;
                    while (true) {
                        status = getTasks();
                        switch (status) {
                            case 0: {
                                pop();
                                return INTERPRET_OK;
                            }
                            case -1: {
                                unsigned int utime = 10000;
                                usleep(utime);
                                continue;
                            }
                            case 1: {
                                load_new_frame();
                                break;
                            }
                        }
                        break;
                    }
                }

                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

ObjModule *interpret(StmtArray *body, const char *name, const char *path) {
    ObjModule *module = newModule(name, path, true);
    if (body == NULL) {
        module->result = INTERPRET_OK;
        return module;
    }
    push(OBJ_VAL(module));
    ObjFunction *function = compile(body);
    if (function == NULL) {
        module->result = INTERPRET_COMPILE_ERROR;
        return module;
    }

    function->module = module;
    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    vm.vmReady = true;

    InterpretResult result = run(module);

    module->result = result;

    pop();
    return module;
}

InterpretResult interpretInModule(StmtArray *body, ObjModule *module) {
    push(OBJ_VAL(module));
    ObjFunction *function = compile(body);
    if (function == NULL) {
        pop();
        return INTERPRET_COMPILE_ERROR;
    }

    function->module = module;
    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    vm.vmReady = true;

    InterpretResult result = run(module);

    pop();
    return result;
}

char *remove_n(char *dst, const char *filename, int n) {
    size_t len = strlen(filename);
    memcpy(dst, filename, len - n);
    dst[len - n] = 0;
    return dst;
}

ObjModule *executeModule(ObjString *relPath, const char *importingPath) {
    ModuleContext temp = moduleContext;
    moduleContext = IMPORT;
    char *path = findModule(relPath->chars, importingPath);

    Value cachedModule;
    if (tableGet(&vm.modules, copyString(path, (int) strlen(path)), &cachedModule)) {
        return AS_MODULE(cachedModule);
    }
    char *source = readFile(path);
    char chars[64];
    const char *importName = relPath->chars;
    if (importName[0] == '@') {
        strncpy(chars, importName + 1, 63);
        chars[63] = '\0';
    } else {
        remove_n(chars, basename(relPath->chars), 4);
    }

    StmtArray *body = parseAST(source);
    if (body == NULL) {
        free(source);
        moduleContext = temp;
        runtimeError("Compile error in module '%s'.", relPath->chars);
        return NULL;
    }
//    evaluateTree(body);
    ObjModule *module = interpret(body, chars, path);
    free(source);
    moduleContext = temp;
    if (module->result == INTERPRET_COMPILE_ERROR) runtimeError("Compile error");
    if (module->result == INTERPRET_RUNTIME_ERROR) runtimeError("Runtime error");

    return module;
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}