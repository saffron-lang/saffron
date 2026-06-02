#ifndef SAFFRON_REGEX_H
#define SAFFRON_REGEX_H

#include "module.h"

ObjModule *createRegexModule();
SimpleType *createRegexModuleType();

extern ModuleRegister regexModuleRegister;

#endif
