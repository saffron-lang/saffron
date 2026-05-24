#include "builtins.h"
#include "../vm.h"
#include "async.h"
#include "io.h"
#include "map.h"
#include "list.h"
#include "task.h"
#include "time.h"
#include "json.h"
#include "reflect.h"
#include "math_mod.h"
#include "random.h"
#include "os.h"
#include "stringbuilder.h"
#include "regex.h"

void initLib() {
    defineType("Module", OBJ_VAL(createModuleType()));
    createListIteratorType();
    createMapIteratorType();
    defineBuiltin("List", OBJ_VAL(createListType()));
    defineBuiltin("Map", OBJ_VAL(createMapType()));
    defineBuiltin("StringBuilder", OBJ_VAL(createStringBuilderType()));
    defineType("Task", OBJ_VAL(createTaskType()));

    #define MODULE_COUNT 9
    ModuleRegister registry[MODULE_COUNT] = {
            timeModuleRegister,
            ioModuleRegister,
            taskModuleRegister,
            jsonModuleRegister,
            reflectModuleRegister,
            mathModuleRegister,
            randomModuleRegister,
            osModuleRegister,
            regexModuleRegister,
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
}