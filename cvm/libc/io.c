#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "io.h"
#include "list.h"
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

static Value listDirNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.list_dir expects 1 string argument (path)");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);
    DIR *dir = opendir(path->chars);
    if (dir == NULL) {
        runtimeError("Could not open directory \"%s\"", path->chars);
        return NIL_VAL;
    }

    ObjList *list = newList();
    push(OBJ_VAL(list));

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        Value name = OBJ_VAL(copyString(entry->d_name, (int)strlen(entry->d_name)));
        writeValueArray(&list->items, name);
    }
    closedir(dir);
    pop();
    return OBJ_VAL(list);
}

static void walkDirRecursive(const char *dirPath, ObjList *results) {
    DIR *dir = opendir(dirPath);
    if (dir == NULL) return;

    ObjList *dirs = newList();
    push(OBJ_VAL(dirs));
    ObjList *files = newList();
    push(OBJ_VAL(files));

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        int pathLen = (int)strlen(dirPath);
        int nameLen = (int)strlen(entry->d_name);
        char fullPath[4096];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

        struct stat st;
        if (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode)) {
            Value name = OBJ_VAL(copyString(entry->d_name, nameLen));
            writeValueArray(&dirs->items, name);
        } else {
            Value name = OBJ_VAL(copyString(entry->d_name, nameLen));
            writeValueArray(&files->items, name);
        }
    }
    closedir(dir);

    ObjList *triple = newList();
    push(OBJ_VAL(triple));
    Value rootVal = OBJ_VAL(copyString(dirPath, (int)strlen(dirPath)));
    writeValueArray(&triple->items, rootVal);
    writeValueArray(&triple->items, OBJ_VAL(dirs));
    writeValueArray(&triple->items, OBJ_VAL(files));
    writeValueArray(&results->items, OBJ_VAL(triple));
    pop(); // triple
    pop(); // files
    pop(); // dirs

    for (int i = 0; i < dirs->items.count; i++) {
        ObjString *subName = AS_STRING(dirs->items.values[i]);
        char subPath[4096];
        snprintf(subPath, sizeof(subPath), "%s/%s", dirPath, subName->chars);
        walkDirRecursive(subPath, results);
    }
}

static Value walkDirNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.walk_dir expects 1 string argument (path)");
        return NIL_VAL;
    }
    ObjString *path = AS_STRING(args[0]);
    ObjList *results = newList();
    push(OBJ_VAL(results));
    walkDirRecursive(path->chars, results);
    pop();
    return OBJ_VAL(results);
}

static Value mkdirNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.mkdir expects 1 string argument (path)");
        return BOOL_VAL(false);
    }
    ObjString *path = AS_STRING(args[0]);
    return BOOL_VAL(mkdir(path->chars, 0755) == 0);
}

static Value renameNative(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("IO.rename expects 2 string arguments (old_path, new_path)");
        return BOOL_VAL(false);
    }
    ObjString *oldPath = AS_STRING(args[0]);
    ObjString *newPath = AS_STRING(args[1]);
    return BOOL_VAL(rename(oldPath->chars, newPath->chars) == 0);
}

static Value fileSizeNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.file_size expects 1 string argument (path)");
        return NUMBER_VAL(-1);
    }
    ObjString *path = AS_STRING(args[0]);
    struct stat st;
    if (stat(path->chars, &st) != 0) {
        return NUMBER_VAL(-1);
    }
    return NUMBER_VAL((double)st.st_size);
}

static Value isDirNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("IO.is_dir expects 1 string argument (path)");
        return BOOL_VAL(false);
    }
    ObjString *path = AS_STRING(args[0]);
    struct stat st;
    if (stat(path->chars, &st) != 0) return BOOL_VAL(false);
    return BOOL_VAL(S_ISDIR(st.st_mode));
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
    defineModuleFunction(module, "list_dir", listDirNative);
    defineModuleFunction(module, "walk_dir", walkDirNative);
    defineModuleFunction(module, "mkdir", mkdirNative);
    defineModuleFunction(module, "rename", renameNative);
    defineModuleFunction(module, "file_size", fileSizeNative);
    defineModuleFunction(module, "is_dir", isDirNative);
    pop();
    return module;
}

SimpleType *createIOModuleType() {
    SimpleType *ioModule = newSimpleType();
    createBuiltinFunctorType(ioModule, "print", (Type *[]) {(Type *) anyType, (Type *) anyType}, 2, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(ioModule, "println", (Type *[]) {(Type *) anyType, (Type *) anyType}, 2, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(ioModule, "read_file", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(ioModule, "write_file", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(ioModule, "append_file", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(ioModule, "file_exists", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(ioModule, "delete_file", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(ioModule, "readline", (Type *[]) {}, 0, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(ioModule, "list_dir", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) listTypeDef);
    createBuiltinFunctorType(ioModule, "walk_dir", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) listTypeDef);
    createBuiltinFunctorType(ioModule, "mkdir", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(ioModule, "rename", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(ioModule, "file_size", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(ioModule, "is_dir", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) boolType);
    return ioModule;
}

ModuleRegister ioModuleRegister = {
        createIOModule,
        createIOModuleType,
        "io",
        "IO",
        true
};