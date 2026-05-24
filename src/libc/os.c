#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "os.h"
#include "list.h"
#include "module.h"

static Value osEnvFn(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("OS.env expects 1 string argument");
        return NIL_VAL;
    }
    const char *val = getenv(AS_STRING(args[0])->chars);
    if (val == NULL) return NIL_VAL;
    return OBJ_VAL(copyString(val, (int)strlen(val)));
}

static Value osSetEnvFn(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("OS.set_env expects 2 string arguments (name, value)");
        return NIL_VAL;
    }
    setenv(AS_STRING(args[0])->chars, AS_STRING(args[1])->chars, 1);
    return NIL_VAL;
}

static Value osCwdFn(int argCount, Value *args) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        runtimeError("OS.cwd: could not get current directory");
        return NIL_VAL;
    }
    return OBJ_VAL(copyString(buf, (int)strlen(buf)));
}

static Value osExitFn(int argCount, Value *args) {
    int code = (argCount > 0) ? (int)AS_NUMBER(args[0]) : 0;
    exit(code);
    return NIL_VAL;
}

static Value osExecFn(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("OS.exec expects 1 string argument (command)");
        return NIL_VAL;
    }
    ObjString *cmd = AS_STRING(args[0]);
    FILE *fp = popen(cmd->chars, "r");
    if (fp == NULL) {
        runtimeError("OS.exec: failed to run command");
        return NIL_VAL;
    }

    char *output = NULL;
    size_t totalLen = 0;
    char buf[1024];
    size_t bytesRead;

    while ((bytesRead = fread(buf, 1, sizeof(buf), fp)) > 0) {
        char *newOutput = realloc(output, totalLen + bytesRead + 1);
        if (newOutput == NULL) {
            free(output);
            pclose(fp);
            runtimeError("OS.exec: out of memory");
            return NIL_VAL;
        }
        output = newOutput;
        memcpy(output + totalLen, buf, bytesRead);
        totalLen += bytesRead;
    }
    pclose(fp);

    if (output == NULL) {
        return OBJ_VAL(copyString("", 0));
    }
    output[totalLen] = '\0';
    ObjString *result = copyString(output, (int)totalLen);
    free(output);
    return OBJ_VAL(result);
}

static Value osPlatformFn(int argCount, Value *args) {
#if defined(__APPLE__)
    return OBJ_VAL(copyString("darwin", 6));
#elif defined(__linux__)
    return OBJ_VAL(copyString("linux", 5));
#elif defined(_WIN32)
    return OBJ_VAL(copyString("windows", 7));
#else
    return OBJ_VAL(copyString("unknown", 7));
#endif
}

static Value osArgsFn(int argCount, Value *args) {
    ObjList *list = newList();
    push(OBJ_VAL(list));
    extern int saffronArgc;
    extern const char **saffronArgv;
    for (int i = 0; i < saffronArgc; i++) {
        Value str = OBJ_VAL(copyString(saffronArgv[i], (int)strlen(saffronArgv[i])));
        writeValueArray(&list->items, str);
    }
    pop();
    return OBJ_VAL(list);
}

ObjModule *createOSModule() {
    ObjModule *module = newModule("OS", "os", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "env", osEnvFn);
    defineModuleFunction(module, "set_env", osSetEnvFn);
    defineModuleFunction(module, "cwd", osCwdFn);
    defineModuleFunction(module, "exit", osExitFn);
    defineModuleFunction(module, "exec", osExecFn);
    defineModuleFunction(module, "platform", osPlatformFn);
    defineModuleFunction(module, "args", osArgsFn);
    pop();
    return module;
}

SimpleType *createOSModuleType() {
    SimpleType *mod = newSimpleType();
    createBuiltinFunctorType(mod, "env", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "set_env", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(mod, "cwd", (Type *[]) {}, 0, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "exit", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(mod, "exec", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "platform", (Type *[]) {}, 0, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "args", (Type *[]) {}, 0, NULL, 0, (Type *) listTypeDef);
    return mod;
}

ModuleRegister osModuleRegister = {
    createOSModule,
    createOSModuleType,
    "os",
    "OS",
    false
};
