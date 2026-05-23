#include <printf.h>
#include <stdlib.h>
#include "io.h"
#include "module.h"
#include "../memory.h"

Value printNative(int argCount, Value *args) {
    for (int i = 0; i < argCount; i++) {
        printValue(args[i]);
        if (i < argCount) {
            printf(" ");
        }
    }
    return NIL_VAL;
}

Value printlnNative(int argCount, Value *args) {
    printNative(argCount, args);
    printf("\n");
    return NIL_VAL;
}

Value readFileNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.read_file expects 1 string argument (path)");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);
    FILE *file = fopen(path->chars, "rb");
    if (file == NULL) {
        runtimeError("Could not open file \"%s\"", path->chars);
        return NIL_VAL;
    }
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = ALLOCATE(char, fileSize + 1);
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);

    ObjString *result = takeString(buffer, (int) bytesRead);
    return OBJ_VAL(result);
}

Value writeFileNative(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("IO.write_file expects 2 string arguments (path, content)");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);
    ObjString *content = AS_STRING(args[1]);
    FILE *file = fopen(path->chars, "wb");
    if (file == NULL) {
        runtimeError("Could not open file \"%s\" for writing", path->chars);
        return NIL_VAL;
    }
    fwrite(content->chars, sizeof(char), content->length, file);
    fclose(file);
    return NIL_VAL;
}

Value appendFileNative(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("IO.append_file expects 2 string arguments (path, content)");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);
    ObjString *content = AS_STRING(args[1]);
    FILE *file = fopen(path->chars, "ab");
    if (file == NULL) {
        runtimeError("Could not open file \"%s\" for appending", path->chars);
        return NIL_VAL;
    }
    fwrite(content->chars, sizeof(char), content->length, file);
    fclose(file);
    return NIL_VAL;
}

Value fileExistsNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.file_exists expects 1 string argument (path)");
        return BOOL_VAL(false);
    }
    ObjString *path = AS_STRING(args[0]);
    FILE *file = fopen(path->chars, "r");
    if (file == NULL) return BOOL_VAL(false);
    fclose(file);
    return BOOL_VAL(true);
}

Value deleteFileNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.delete_file expects 1 string argument (path)");
        return BOOL_VAL(false);
    }
    ObjString *path = AS_STRING(args[0]);
    return BOOL_VAL(remove(path->chars) == 0);
}

Value readlineNative(int argCount, Value *args) {
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NIL_VAL;
    }
    int len = (int) strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[--len] = '\0';
    }
    return OBJ_VAL(copyString(buffer, len));
}

ObjModule *createIOModule() {
    ObjModule *module = newModule("IO", "io", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "print", printNative);
    defineModuleFunction(module, "println", printlnNative);
    defineModuleFunction(module, "read_file", readFileNative);
    defineModuleFunction(module, "write_file", writeFileNative);
    defineModuleFunction(module, "append_file", appendFileNative);
    defineModuleFunction(module, "file_exists", fileExistsNative);
    defineModuleFunction(module, "delete_file", deleteFileNative);
    defineModuleFunction(module, "readline", readlineNative);
    pop();
    return module;
}

SimpleType *createIOModuleType() {
    SimpleType *ioModule = newSimpleType();
    createBuiltinFunctorType(ioModule, "print", (Type *[]) {anyType, anyType}, 2, NULL, 0, nilType);
    createBuiltinFunctorType(ioModule, "println", (Type *[]) {anyType, anyType}, 2, NULL, 0, nilType);
    createBuiltinFunctorType(ioModule, "read_file", (Type *[]) {stringType}, 1, NULL, 0, stringType);
    createBuiltinFunctorType(ioModule, "write_file", (Type *[]) {stringType, stringType}, 2, NULL, 0, nilType);
    createBuiltinFunctorType(ioModule, "append_file", (Type *[]) {stringType, stringType}, 2, NULL, 0, nilType);
    createBuiltinFunctorType(ioModule, "file_exists", (Type *[]) {stringType}, 1, NULL, 0, boolType);
    createBuiltinFunctorType(ioModule, "delete_file", (Type *[]) {stringType}, 1, NULL, 0, boolType);
    createBuiltinFunctorType(ioModule, "readline", (Type *[]) {}, 0, NULL, 0, stringType);
    return ioModule;
}

ModuleRegister ioModuleRegister = {
        createIOModule,
        createIOModuleType,
        "io",
        "IO",
        true
};