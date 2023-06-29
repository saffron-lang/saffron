#include <math.h>
#include "async.h"
#include "list.h"
#include "time.h"
#include "task.h"

AsyncHandler asyncHandler;

Value spawnNative(int argCount, Value *args) {
    if (!IS_CLOSURE(args[0])) {
        runtimeError("Invalid argument for parameter 0, expect a function");
        return NIL_VAL;
    }

    ObjClosure *closure = AS_CLOSURE(args[0]);

    ObjCallFrame *frame = ALLOCATE_OBJ(ObjCallFrame, OBJ_CALL_FRAME);
    writeValueArray(&vm.tasks, OBJ_VAL(frame));
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack;
    frame->state = SPAWNED;
    frame->stored = NIL_VAL;

    initValueArray(&frame->stack);
    writeValueArray(&frame->stack, args[0]);

    frame->result = NIL_VAL;
    frame->parent = NULL;
    frame->index = currentFrame->index + 1;

    return OBJ_VAL(newTask(frame));
}

void initAsyncHandler() {
    initValueArray(&asyncHandler.sleepers);
    initValueArray(&asyncHandler.sleeper_times);
}

void freeAsyncHandler() {
    freeValueArray(&asyncHandler.sleepers);
    freeValueArray(&asyncHandler.sleeper_times);
}

void markAsyncRoots() {
    markArray(&asyncHandler.sleepers);
    markArray(&asyncHandler.sleeper_times);
}

void handle_yield_value(Value value) {
    if (IS_LIST(value)) {
        ObjList *list = AS_LIST(value);
        Value arg = getListItem(list, 0);
        if (valuesEqual(arg, NIL_VAL) || !IS_NUMBER(arg)) {
            runtimeError("Yielded invalid type");
        }

        int op = trunc(AS_NUMBER(arg));

        switch (op) {
            case SLEEP: {
                Value timeArg = getListItem(list, 1);
                if (valuesEqual(arg, NIL_VAL) || !IS_NUMBER(timeArg)) {
                    runtimeError("Yielded invalid type");
                }

                double time = AS_NUMBER(timeArg);

                writeValueArray(&asyncHandler.sleepers, OBJ_VAL(currentFrame));
                writeValueArray(&asyncHandler.sleeper_times, NUMBER_VAL(getTime() + time));

                popValueArray(&vm.tasks, vm.currentTask);
                if (vm.currentTask >= vm.tasks.count) {
                    getTasks();
                }
                vm.currentTask = vm.currentTask % vm.tasks.count;

                break;
            }
            default:
                runtimeError("Invalid yield op %d", op);
                return;
        }
    } else {
        if ((vm.currentTask + 1) >= vm.tasks.count) {
            getTasks();
        }
        vm.currentTask = (vm.currentTask + 1) % vm.tasks.count;
    }
}

// TODO: Make the queue a heapq
int getTasks() {
    if (!asyncHandler.sleepers.count) {
        return 0;
    } else {
        int found = -1;
        for (int i = 0; i < asyncHandler.sleepers.count; i++) {
//            printf("Sleeper time: %f %f\n", AS_NUMBER(asyncHandler.sleeper_times.values[i]), getTime());
            if (AS_NUMBER(asyncHandler.sleeper_times.values[i]) < getTime()) {
                popValueArray(&asyncHandler.sleeper_times, i);
                Value sleeper = asyncHandler.sleepers.values[i];
                AS_CALL_FRAME(sleeper)->stored = BOOL_VAL(true);
                writeValueArray(&vm.tasks, sleeper);
                popValueArray(&asyncHandler.sleepers, i);
                found = 1;
                i--;
            }
        }

        return found;
    }
}