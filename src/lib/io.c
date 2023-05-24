#include <printf.h>
#include "io.h"

Value printNative(int argCount, Value* args) {
    for (int i = 0; i < argCount; i++) {
        printValue(args[i]);
    }
    return NIL_VAL;
}

Value printlnNative(int argCount, Value* args) {
    printNative(argCount, args);
    printf("\n");
    return NIL_VAL;
}