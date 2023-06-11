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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <libc.h>
#include <libgen.h>

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

void initVM() {
    resetStack();
    vm.vmReady = false;

    vm.currentTask = 0;
    vm.objects = NULL;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    initTable(&vm.types);
    initTable(&vm.modules);
    initTable(&vm.builtins);
    initTable(&vm.strings);

    vm.initString = NULL;
    vm.initString = copyString("init", 4);
    vm.openUpvalues = NULL;

    defineBuiltin("List", OBJ_VAL(createListType()));

    defineNative("println", printlnNative);
    defineNative("print", printNative);

    defineNative("spawn", spawnNative);

    defineType("Task", OBJ_VAL(createTaskType()));
    defineType("Module", OBJ_VAL(createModuleType()));

    defineModule("time", OBJ_VAL(createTimeModule()));
//    defineNative("sleep", sleepNative);

    initAsyncHandler();
}

void freeVM() {
    freeAsyncHandler();

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

void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (ObjCallFrame *frame = CURRENT_TASK; frame->parent != NULL; frame = frame->parent) {
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

ObjModule *executeModule(ObjString *name);

static bool call(ObjClosure *closure, int argCount) {
    switch (closure->obj.type) {
        case OBJ_CLOSURE: {
            if (argCount != closure->function->arity) {
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

    if (!(IS_INSTANCE(receiver) || IS_LIST(receiver))) {
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
                tableSet(&module->obj.fields, name, peek(0));
                pop();
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (!tableGet(&module->obj.fields, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                if (tableSet(&module->obj.fields, name, peek(0))) {
                    tableDelete(&module->obj.fields, name);
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
                currentFrame = CURRENT_TASK;
                break;
            }
            case OP_GETITEM: {
                int index = trunc(AS_NUMBER(pop()));
                ObjList *list = AS_OBJ(pop());
                push(*getItem(list, index));
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
            case OP_FIELD:
                defineField(READ_STRING());
                break;
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
                ObjModule *newModule = executeModule(AS_STRING(relPath));
                tableSet(&module->obj.fields, newModule->name, OBJ_VAL(newModule));
                pop();
                break;
            }
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
    push(OBJ_VAL(module));
    ObjFunction *function = compile(body);
    if (function == NULL) {
        module->result = INTERPRET_COMPILE_ERROR;
        return module;
    }

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

char *remove_n(char *dst, const char *filename, int n) {
    size_t len = strlen(filename);
    memcpy(dst, filename, len - n);
    dst[len - n] = 0;
    return dst;
}

ObjModule *executeModule(ObjString *relPath) {
    ModuleContext temp = moduleContext;
    moduleContext = IMPORT;
    char *path = findModule(relPath->chars);

    Value cachedModule;
    if (tableGet(&vm.modules, copyString(path, (int) strlen(path)), &cachedModule)) {
        return AS_MODULE(cachedModule);
    }
    char *source = readFile(path);
    char chars[64];
    remove_n(chars, basename(relPath->chars), 4);

    StmtArray *body = parseAST(source);
    evaluateTree(body);
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