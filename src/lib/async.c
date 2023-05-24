#include <math.h>
#include "async.h"
#include "../memory.h"
#include "list.h"

AsyncHandler asyncHandler;

Value spawn(int argCount, Value *args) {
    ObjClosure *closure = AS_CLOSURE(args[0]);

    ObjCallFrame *frame = ALLOCATE_OBJ(ObjCallFrame, OBJ_CALL_FRAME);
    writeValueArray(&vm.tasks, OBJ_VAL(frame));
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack;
    frame->state = SPAWNED;
    frame->stored = NIL_VAL;

    initValueArray(&frame->stack);
//    printf("EPIC ARGCOUNT, %d\n", argCount);
    for (int i = 0; i < argCount; i++) {
        writeValueArray(&frame->stack, args[i]);
    }

    frame->result = NIL_VAL;
    frame->parent = NULL;
    frame->index = currentFrame->index + 1;

    return NIL_VAL;
}

void initAsyncHandler() {
    initValueArray(&asyncHandler.sleepers);
}

void freeAsyncHandler() {
    freeValueArray(&asyncHandler.sleepers);
}

void markAsyncRoots() {
    markArray(&asyncHandler.sleepers);
}

void handle_yield_value(Value value) {
    if (IS_LIST(value)) {
        ObjList *list = AS_LIST(value);
        Value *arg = getItem(list, 0);
        if (arg == NULL || !IS_NUMBER(*arg)) {
            runtimeError("Yielded invalid type");
        }

        int op = trunc(AS_NUMBER(*arg));
        switch (op) {
            case SLEEP: {
                writeValueArray(&asyncHandler.sleepers, currentFrame)
                break;
            }
            default:
                runtimeError("Invalid op");
                return;
        }
    }
}
