#include "common.h"
#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "lib/time.h"
#include "lib/list.h"
#include "lib/io.h"
#include "lib/async.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

VM vm;

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void resetStack() {
    vm.stackTop = vm.stack;
    initValueArray(&vm.frames);
}

void defineNative(const char *name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void defineGlobal(const char *name, Value value) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(value);
    tableSet(&vm.globals, AS_STRING(peek(1)), peek(0));
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

void initVM() {
    resetStack();

    vm.currentFrame = 0;
    vm.objects = NULL;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    initTable(&vm.globals);
    initTable(&vm.builtins);
    initTable(&vm.strings);

    vm.initString = NULL;
    vm.initString = copyString("init", 4);
    vm.openUpvalues = NULL;

    defineBuiltin("list", OBJ_VAL(createListType()));
    defineGlobal("list", OBJ_VAL(createListType()));
//    defineGlobal("list", OBJ_VAL(type));
    defineNative("clock", clockNative);
    defineNative("println", printlnNative);
    defineNative("print", printNative);

    defineBuiltin("generator", OBJ_VAL(createGeneratorType()));
    defineNative("yield2", yield);
    defineNative("spawn", spawn);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.builtins);
    freeTable(&vm.strings);
    vm.initString = NULL;
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

void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (ObjCallFrame *frame = CURRENT_FRAME; frame->parent != NULL; frame = frame->parent) {
        ObjFunction *function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

static bool call(ObjClosure *closure, int argCount) {
    switch (closure->obj.type) {
        case OBJ_CLOSURE: {
            if (argCount != closure->function->arity) {
                runtimeError("Expected %d arguments but got %d.",
                             closure->function->arity, argCount);
                return false;
            }

            if (vm.frames.count == FRAMES_MAX) {
                runtimeError("Stack overflow.");
                return false;
            }

            ObjCallFrame *frame = ALLOCATE_OBJ(ObjCallFrame, OBJ_CALL_FRAME);
            frame->closure = closure;
            frame->ip = closure->function->chunk.code;
            frame->slots = vm.stackTop - argCount - 1;
            frame->state = EXECUTING;
            frame->stored = NIL_VAL;

            initValueArray(&frame->stack);
            frame->result = NIL_VAL;

            if (vm.frames.count == 0) {
                frame->parent = NULL;
                frame->index = 0;
                writeValueArray(&vm.frames, OBJ_VAL(frame));
            } else {
                frame->parent = CURRENT_FRAME;
                frame->index = frame->parent->index + 1;
                vm.frames.values[vm.currentFrame] = OBJ_VAL(frame);
            }

            return true;
        }
        case OBJ_NATIVE_METHOD: {
            ObjNativeMethod *nativeMethod = (ObjNativeMethod *) closure;
            NativeMethodFn native = nativeMethod->function;
            Value result = native(AS_OBJ(peek(argCount)), argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(result);
        }
    }
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
    tableSet(&klass->methods, name, method);
    pop();
}

static bool bindMethod(ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
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

    return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString *name, int argCount) {
    Value receiver = peek(argCount);

    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance *instance = AS_INSTANCE(receiver);

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

ObjCallFrame *currentFrame;

static void load_new_frame() {
    popValueArray(&vm.frames, vm.currentFrame);                                                           \
    vm.currentFrame = 0;

    if (CURRENT_FRAME) {
        ObjCallFrame *cfr = CURRENT_FRAME;

        CURRENT_FRAME->state = (CURRENT_FRAME->state & (SPAWNED | GENERATOR)) | EXECUTING;
        CURRENT_FRAME->slots = vm.stackTop;

        for (int i = 0; i < CURRENT_FRAME->stack.count; i++) {
            printValue(CURRENT_FRAME->stack.values[i]);
            printf("\n");
            push(CURRENT_FRAME->stack.values[i]);
        }

        freeValueArray(&CURRENT_FRAME->stack);

        push(NIL_VAL);
    }
}

static void POP_CALL() {
    if (CURRENT_FRAME->state & SPAWNED) {
        load_new_frame();
    } else {
        if (CURRENT_FRAME->parent == NULL) {
            load_new_frame();
        } else {
            vm.frames.values[vm.currentFrame] = OBJ_VAL(CURRENT_FRAME->parent);
        }

    }
}

static InterpretResult run() {
    currentFrame = AS_CALL_FRAME(vm.frames.values[vm.frames.count - 1]);

#define READ_BYTE() (*currentFrame->ip++)

#define READ_SHORT() \
    (currentFrame->ip += 2, \
    (uint16_t)((currentFrame->ip[-2] << 8) | currentFrame->ip[-1]))

#define READ_CONSTANT() \
    (currentFrame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
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
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
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
                } else {
                    runtimeError(
                            "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT:
                BINARY_OP(NUMBER_VAL, -);
                break;
            case OP_MULTIPLY:
                BINARY_OP(NUMBER_VAL, *);
                break;
            case OP_DIVIDE:
                BINARY_OP(NUMBER_VAL, /);
                break;
            case OP_NIL:
                push(NIL_VAL);
                break;
            case OP_TRUE:
                push(BOOL_VAL(true));
                break;
            case OP_FALSE:
                push(BOOL_VAL(false));
                break;
            case OP_GREATER:
                BINARY_OP(BOOL_VAL, >);
                break;
            case OP_LESS:
                BINARY_OP(BOOL_VAL, <);
                break;
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_POP:
                pop();
                break;
            case OP_DEFINE_GLOBAL: {
                ObjString *name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
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
                currentFrame = CURRENT_FRAME;
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

                currentFrame = CURRENT_FRAME;
                break;
            }
            case OP_LIST: {
                int argCount = READ_BYTE();
                ObjList *list = newList();
                push(OBJ_VAL(list));
                for (int i = 1; i < argCount + 1; i++) {
                    listPush(list, peek(i));
                }
                for (int i = 0; i < argCount + 1; i++) {
                    pop();
                }
                push(OBJ_VAL(list));
                break;
            }
            case OP_CLOSURE: {
                ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
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
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
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
            case OP_INVOKE: {
                ObjString *method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                currentFrame = CURRENT_FRAME;
                break;
            }
            case OP_INHERIT: {
                Value superclass = peek(1);
                if (!IS_CLASS(superclass) && !IS_BUILTIN_TYPE(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClass *subclass = AS_CLASS(peek(0));
                tableAddAll(&AS_CLASS(superclass)->methods,
                            &subclass->methods);
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

                currentFrame = CURRENT_FRAME;
                break;
            }
            case OP_YIELD: {
                Value value = pop();
                pop();

                ObjTask *generator = newTask(currentFrame);
//                printf("GENERATING GENERATOR %p\n", generator);
                push(OBJ_VAL(generator));
                generator->frame = currentFrame;
                Value oldStored = currentFrame->stored;
                currentFrame->stored = value;

                Value *stackBottom = currentFrame->slots;
                int i = 0;
                while (&stackBottom[i] <= vm.stackTop) {
                    writeValueArray(&currentFrame->stack, stackBottom[i]);
                    i++;
                }

                pop();

                vm.stackTop = currentFrame->slots - 1;
                int s = currentFrame->state ^ SPAWNED;
                if (!(currentFrame->state & SPAWNED)) {
                    if (currentFrame->state ^ GENERATOR) {
                        push(OBJ_VAL(generator));
                    } else {
                        push(oldStored);
                    }
//                    currentFrame = currentFrame->parent;
                    vm.currentFrame = (vm.currentFrame + 1) % vm.frames.count;
                    currentFrame = CURRENT_FRAME;
                    vm.frames.values[vm.currentFrame] = OBJ_VAL(currentFrame);
                }

                ObjCallFrame *nframe = currentFrame;
                VM *xvm = &vm;
//                ObjCallFrame *nframe2 = CURRENT_FRAME;
                printf("count %d\n", vm.frames.count);
                currentFrame->state = (currentFrame->state & SPAWNED) | PAUSED | GENERATOR;

                break;
            }

            case OP_RESUME: {
                Value arg = pop();
                ObjTask *generator = AS_GENERATOR(pop());
                generator->frame->state = (generator->frame->state & (GENERATOR | SPAWNED)) | EXECUTING | GENERATOR;
                generator->frame->parent = currentFrame;
                generator->frame->slots = vm.stackTop + 1;

                for (int i = 0; i < generator->frame->stack.count; i++) {
                    push(generator->frame->stack.values[i]);
                }

                freeValueArray(&generator->frame->stack);

                push(arg);

                currentFrame = generator->frame;
                vm.frames.values[vm.currentFrame] = OBJ_VAL(generator->frame);

                break;
            }
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(currentFrame->slots);

                printf("epic count %d\n", vm.frames.count);
                if (currentFrame->state & GENERATOR) {
                    POP_CALL();
                    if (vm.frames.count == 0) {
                        pop();
                        return INTERPRET_OK;
                    }

                    currentFrame->result = result;
                    vm.stackTop = currentFrame->slots;
                    if (currentFrame->state ^ SPAWNED) {
                        push(currentFrame->stored);
                    }
                    currentFrame = CURRENT_FRAME;
                } else {
                    POP_CALL();
                    if (currentFrame == NULL) {
                        pop();
                        return INTERPRET_OK;
                    }

                    vm.stackTop = currentFrame->slots;
                    push(result);
                    currentFrame = CURRENT_FRAME;
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

InterpretResult interpret(const char *source) {
    ObjFunction *function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}