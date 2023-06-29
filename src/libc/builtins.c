#include "builtins.h"
#include "../vm.h"
#include "async.h"
#include "io.h"
#include "map.h"
#include "list.h"
#include "task.h"
#include "time.h"

void initLib() { // TODO: Don't evaluate registered modules until accessed?
    defineType("Module", OBJ_VAL(createModuleType()));
    defineBuiltin("List", OBJ_VAL(createListType()));
    defineBuiltin("Map", OBJ_VAL(createMapType()));
    defineType("Task", OBJ_VAL(createTaskType()));

    #define MODULE_COUNT 3
    ModuleRegister registry[MODULE_COUNT] = {
            timeModuleRegister,
            ioModuleRegister,
            taskModuleRegister,
    };

    for (int i = 0; i < MODULE_COUNT; i++) {
        ModuleRegister reg = registry[i];
        ObjModule* module = reg.createModuleFn();
        defineModule(reg.path, OBJ_VAL(module));
        if (reg.builtin){
            defineBuiltin(reg.name, OBJ_VAL(module));
        }

        defineBuiltinTypeDef(reg.path, reg.name,reg.createModuleTypeFn(), reg.builtin);
    }


//    defineNative("sleep", sleepNative);
}