#include "time.h"
#include "module.h"
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

double getTime() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    return (double) start.tv_sec + (double) start.tv_nsec / 1e9;
}

Value clockNative(int argCount, Value *args) {
    if (argCount > 0) {
        runtimeError("Too many args, expected 0");
    }

    return NUMBER_VAL(getTime());
}

static Value timestampNative(int argCount, Value *args) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return NUMBER_VAL((double)tv.tv_sec + (double)tv.tv_usec / 1e6);
}

static Value sleepNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("Time.sleep expects 1 argument (seconds)");
        return NIL_VAL;
    }
    double seconds = AS_NUMBER(args[0]);
    unsigned int usec = (unsigned int)(seconds * 1000000);
    usleep(usec);
    return NIL_VAL;
}

static Value toIsoNative(int argCount, Value *args) {
    time_t t;
    if (argCount == 0) {
        t = time(NULL);
    } else {
        t = (time_t)AS_NUMBER(args[0]);
    }
    struct tm *tm_info = gmtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    return OBJ_VAL(copyString(buf, (int)strlen(buf)));
}

static Value yearNative(int argCount, Value *args) {
    time_t t = (argCount > 0) ? (time_t)AS_NUMBER(args[0]) : time(NULL);
    struct tm *tm_info = localtime(&t);
    return NUMBER_VAL((double)(tm_info->tm_year + 1900));
}

static Value monthNative(int argCount, Value *args) {
    time_t t = (argCount > 0) ? (time_t)AS_NUMBER(args[0]) : time(NULL);
    struct tm *tm_info = localtime(&t);
    return NUMBER_VAL((double)(tm_info->tm_mon + 1));
}

static Value dayNative(int argCount, Value *args) {
    time_t t = (argCount > 0) ? (time_t)AS_NUMBER(args[0]) : time(NULL);
    struct tm *tm_info = localtime(&t);
    return NUMBER_VAL((double)tm_info->tm_mday);
}

static Value hourNative(int argCount, Value *args) {
    time_t t = (argCount > 0) ? (time_t)AS_NUMBER(args[0]) : time(NULL);
    struct tm *tm_info = localtime(&t);
    return NUMBER_VAL((double)tm_info->tm_hour);
}

static Value minuteNative(int argCount, Value *args) {
    time_t t = (argCount > 0) ? (time_t)AS_NUMBER(args[0]) : time(NULL);
    struct tm *tm_info = localtime(&t);
    return NUMBER_VAL((double)tm_info->tm_min);
}

static Value secondNative(int argCount, Value *args) {
    time_t t = (argCount > 0) ? (time_t)AS_NUMBER(args[0]) : time(NULL);
    struct tm *tm_info = localtime(&t);
    return NUMBER_VAL((double)tm_info->tm_sec);
}

ObjModule *createTimeModule() {
    ObjModule *module = newModule("Time", "time", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "clock", clockNative);
    defineModuleFunction(module, "timestamp", timestampNative);
    defineModuleFunction(module, "sleep", sleepNative);
    defineModuleFunction(module, "to_iso", toIsoNative);
    defineModuleFunction(module, "year", yearNative);
    defineModuleFunction(module, "month", monthNative);
    defineModuleFunction(module, "day", dayNative);
    defineModuleFunction(module, "hour", hourNative);
    defineModuleFunction(module, "minute", minuteNative);
    defineModuleFunction(module, "second", secondNative);
    pop();
    return module;
}

SimpleType *createTimeModuleType() {
    SimpleType *timeModule = newSimpleType();
    createBuiltinFunctorType(timeModule, "clock", NULL, 0, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "timestamp", NULL, 0, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "sleep", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) nilType);
    createBuiltinFunctorType(timeModule, "to_iso", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(timeModule, "year", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "month", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "day", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "hour", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "minute", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(timeModule, "second", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    return timeModule;
}

ModuleRegister timeModuleRegister = {
        createTimeModule,
        createTimeModuleType,
        "time",
        "Time",
        false
};