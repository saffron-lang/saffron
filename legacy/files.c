#include "files.h"

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <mach-o/dyld.h>

static char *getExecutableDir() {
    static char exeDir[PATH_MAX] = {0};
    if (exeDir[0] != '\0') return exeDir;

    uint32_t size = PATH_MAX;
    char exePath[PATH_MAX];
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        char resolved[PATH_MAX];
        if (realpath(exePath, resolved)) {
            strncpy(exeDir, resolved, PATH_MAX);
            char *lastSlash = strrchr(exeDir, '/');
            if (lastSlash) *lastSlash = '\0';
            return exeDir;
        }
    }
    strcpy(exeDir, ".");
    return exeDir;
}

char *findModule(const char *relPath, const char *importingFilePath) {
    static char resolved[PATH_MAX];
    static char canonical[PATH_MAX];

    // 1. Stdlib prefix: "@name" resolves to <exe_dir>/../src/lib/<name>.sf
    if (relPath[0] == '@') {
        const char *name = relPath + 1;
        char *exeDir = getExecutableDir();
        snprintf(resolved, PATH_MAX, "%s/../src/lib/%s.sf", exeDir, name);
        if (access(resolved, F_OK) == 0) {
            if (realpath(resolved, canonical)) return canonical;
            return resolved;
        }
    }

    // 2. Try relative to the importing file's directory
    if (importingFilePath != NULL) {
        char importingDir[PATH_MAX];
        strncpy(importingDir, importingFilePath, PATH_MAX);
        char *lastSlash = strrchr(importingDir, '/');
        if (lastSlash) {
            *lastSlash = '\0';
        } else {
            strcpy(importingDir, ".");
        }

        snprintf(resolved, PATH_MAX, "%s/%s", importingDir, relPath);
        if (access(resolved, F_OK) == 0) {
            if (realpath(resolved, canonical)) return canonical;
            return resolved;
        }
    }

    // 3. Try relative to stdlib: <exe_dir>/../src/ (legacy "lib/X.sf" paths)
    char *exeDir = getExecutableDir();
    snprintf(resolved, PATH_MAX, "%s/../src/%s", exeDir, relPath);
    if (access(resolved, F_OK) == 0) {
        if (realpath(resolved, canonical)) return canonical;
        return resolved;
    }

    // 4. Fall back to the path as given (CWD-relative)
    strncpy(resolved, relPath, PATH_MAX);
    return resolved;
}

char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *) malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}
