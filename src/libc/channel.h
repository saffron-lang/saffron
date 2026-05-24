#ifndef SAFFRON_CHANNEL_H
#define SAFFRON_CHANNEL_H

#include "../object.h"
#include "type.h"

typedef struct {
    ObjInstance obj;
    ValueArray buffer;
    int capacity;
    bool closed;
} ObjChannel;

ObjChannel *newChannel(int capacity);
void freeChannel(ObjChannel *ch);
void markChannel(ObjChannel *ch);
ObjBuiltinType *createChannelType();

#endif
