#include <stdio.h>
#include <string.h>
#include "channel.h"
#include "../memory.h"
#include "../vm.h"

static ObjBuiltinType *channelType = NULL;

ObjChannel *newChannel(int capacity) {
    ObjChannel *ch = ALLOCATE_OBJ(ObjChannel, OBJ_INSTANCE);
    ch->obj.klass = (ObjClass *) channelType;
    initTable(&ch->obj.fields);
    initValueArray(&ch->buffer);
    ch->capacity = capacity > 0 ? capacity : 256;
    ch->closed = false;
    return ch;
}

void freeChannel(ObjChannel *ch) {
    freeValueArray(&ch->buffer);
    FREE(ObjChannel, ch);
}

void markChannel(ObjChannel *ch) {
    markArray(&ch->buffer);
}

static void printChannel(ObjChannel *ch) {
    printf("<Channel size=%d cap=%d%s>", ch->buffer.count, ch->capacity, ch->closed ? " closed" : "");
}

static Value channelSend(Obj *self, int argCount, Value *args) {
    ObjChannel *ch = (ObjChannel *)self;
    if (ch->closed) {
        runtimeError("Cannot send on closed channel");
        return NIL_VAL;
    }
    if (argCount < 1) {
        runtimeError("Channel.send expects 1 argument");
        return NIL_VAL;
    }
    if (ch->buffer.count >= ch->capacity) {
        runtimeError("Channel buffer full");
        return NIL_VAL;
    }
    writeValueArray(&ch->buffer, args[0]);
    return NIL_VAL;
}

static Value channelReceive(Obj *self, int argCount, Value *args) {
    ObjChannel *ch = (ObjChannel *)self;
    if (ch->buffer.count == 0) {
        return NIL_VAL;
    }
    Value val = ch->buffer.values[0];
    // Shift buffer left
    for (int i = 1; i < ch->buffer.count; i++) {
        ch->buffer.values[i - 1] = ch->buffer.values[i];
    }
    ch->buffer.count--;
    return val;
}

static Value channelHasData(Obj *self, int argCount, Value *args) {
    ObjChannel *ch = (ObjChannel *)self;
    return BOOL_VAL(ch->buffer.count > 0);
}

static Value channelSize(Obj *self, int argCount, Value *args) {
    ObjChannel *ch = (ObjChannel *)self;
    return NUMBER_VAL(ch->buffer.count);
}

static Value channelClose(Obj *self, int argCount, Value *args) {
    ObjChannel *ch = (ObjChannel *)self;
    ch->closed = true;
    return NIL_VAL;
}

static Value channelIsClosed(Obj *self, int argCount, Value *args) {
    ObjChannel *ch = (ObjChannel *)self;
    return BOOL_VAL(ch->closed);
}

static Value channelCall(int argCount, Value *args) {
    int capacity = 256;
    if (argCount >= 1 && IS_NUMBER(args[0])) {
        capacity = (int)AS_NUMBER(args[0]);
    }
    ObjChannel *ch = newChannel(capacity);
    return OBJ_VAL(ch);
}

static void channelInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeChannel;
    type->markFn = (MarkFn) &markChannel;
    type->printFn = (PrintFn) &printChannel;
    type->typeCallFn = (TypeCallFn) &channelCall;
    type->typeDefFn = NULL;
    defineBuiltinMethod(type, "send", (NativeMethodFn) channelSend);
    defineBuiltinMethod(type, "receive", (NativeMethodFn) channelReceive);
    defineBuiltinMethod(type, "has_data?", (NativeMethodFn) channelHasData);
    defineBuiltinMethod(type, "size", (NativeMethodFn) channelSize);
    defineBuiltinMethod(type, "close", (NativeMethodFn) channelClose);
    defineBuiltinMethod(type, "is_closed?", (NativeMethodFn) channelIsClosed);
}

ObjBuiltinType *createChannelType() {
    channelType = newBuiltinType("Channel", channelInit);
    return channelType;
}
