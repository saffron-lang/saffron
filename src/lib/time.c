#include "time.h"
#include "../vm.h"

Value clockNative(int argCount, Value* args) {
    if (argCount > 0) {
        runtimeError("Too many args, expected 0");
    }

    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}