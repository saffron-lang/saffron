#ifndef SAFFRON_STRINGBUILDER_H
#define SAFFRON_STRINGBUILDER_H

#include "../object.h"
#include "type.h"

typedef struct {
    ObjInstance obj;
    char *chars;
    int length;
    int capacity;
} ObjStringBuilder;

ObjStringBuilder *newStringBuilder();
void freeStringBuilder(ObjStringBuilder *sb);
void markStringBuilder(ObjStringBuilder *sb);
ObjBuiltinType *createStringBuilderType();

#endif
