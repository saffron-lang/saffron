#include "../value.h"
#include "module.h"

Value printlnNative(int argCount, Value* args);
Value printNative(int argCount, Value* args);

ModuleRegister ioModuleRegister;