#include "ps3_file_system.h"
#include "../json_reader.h"
#include "../utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "stb_ds.h"

typedef struct {
    char* key; // game-relative file name
    char** value; // stb_ds dynamic array of resolved device paths
} Ps3FileMapping;

typedef struct {
    FileSystem base;
    Ps3FileMapping* mappings; // stb_ds string hashmap
    char* gameTitle;
} Ps3FileSystem;

static char* resolvePath(FileSystem* fs, const char* relativePath) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return NULL;

    if (arrlen(pfs->mappings[idx].value) > 0)
        return safeStrdup(pfs->mappings[idx].value[0]);

    return NULL;
}

static bool fileExists(FileSystem* fs, const char* relativePath) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return false;

    char** paths = pfs->mappings[idx].value;
    int pathCount = (int)arrlen(paths);
    for (int i = 0; i < pathCount; i++) {
        FILE* f = fopen(paths[i], "rb");
        if (f != NULL) {
            fclose(f);
            return true;
        }
    }

    return false;
}

static char* readFileText(FileSystem* fs, const char* relativePath) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return NULL;

    char** paths = pfs->mappings[idx].value;
    int pathCount = (int)arrlen(paths);
    for (int i = 0; i < pathCount; i++) {
        FILE* f = fopen(paths[i], "rb");
        if (f == NULL)
            continue;

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* content = safeMalloc((size_t) size + 1);
        size_t bytesRead = fread(content, 1, (size_t) size, f);
        content[bytesRead] = '\0';
        fclose(f);
        return content;
    }

    return NULL;
}

static bool writeFileText(FileSystem* fs, const char* relativePath, const char* contents) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return false;

    char** paths = pfs->mappings[idx].value;
    if (arrlen(paths) == 0)
        return false;

    const char* writePath = paths[0];
    FILE* f = fopen(writePath, "wb");
    if (f == nullptr)
        return false;

    size_t len = strlen(contents);
    size_t written = fwrite(contents, 1, len, f);
    fclose(f);
    return written == len;
}

static bool deleteFile(FileSystem* fs, const char* relativePath) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return false;

    char** paths = pfs->mappings[idx].value;
    if (arrlen(paths) == 0)
        return false;

    return remove(paths[0]) == 0;
}

static bool ps3ReadFileBinary(FileSystem* fs, const char* relativePath, uint8_t** outData, int32_t* outSize) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return false;

    char** paths = pfs->mappings[idx].value;
    int pathCount = (int)arrlen(paths);
    for (int i = 0; i < pathCount; i++) {
        FILE* f = fopen(paths[i], "rb");
        if (f == NULL)
            continue;

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint8_t* data = safeMalloc((size_t) size);
        size_t bytesRead = fread(data, 1, (size_t) size, f);
        fclose(f);

        *outData = data;
        *outSize = (int32_t) bytesRead;
        return true;
    }

    return false;
}

static bool ps3WriteFileBinary(FileSystem* fs, const char* relativePath, const uint8_t* data, int32_t size) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    ptrdiff_t idx = shgeti(pfs->mappings, relativePath);
    if (0 > idx)
        return false;

    char** paths = pfs->mappings[idx].value;
    if (arrlen(paths) == 0)
        return false;

    const char* writePath = paths[0];
    FILE* f = fopen(writePath, "wb");
    if (f == NULL)
        return false;

    size_t written = fwrite(data, 1, (size_t) size, f);
    fclose(f);
    return written == (size_t) size;
}

static FileSystemVtable ps3FileSystemVtable = {
    .resolvePath = resolvePath,
    .fileExists = fileExists,
    .readFileText = readFileText,
    .writeFileText = writeFileText,
    .deleteFile = deleteFile,
    .readFileBinary = ps3ReadFileBinary,
    .writeFileBinary = ps3WriteFileBinary,
};

FileSystem* Ps3FileSystem_create(JsonValue* configRoot, const char* gameTitle) {
    JsonValue* fileSystemObj = JsonReader_getObject(configRoot, "fileSystem");
    if (fileSystemObj == NULL || !JsonReader_isObject(fileSystemObj)) {
        return NULL;
    }

    Ps3FileSystem* pfs = safeCalloc(1, sizeof(Ps3FileSystem));
    pfs->base.vtable = &ps3FileSystemVtable;
    pfs->gameTitle = safeStrdup(gameTitle);
    pfs->mappings = NULL;
    sh_new_strdup(pfs->mappings);

    int entryCount = JsonReader_objectLength(fileSystemObj);
    for (int i = 0; i < entryCount; i++) {
        const char* gameFileName = JsonReader_getObjectKey(fileSystemObj, i);
        JsonValue* pathArray = JsonReader_getObjectValue(fileSystemObj, i);

        if (!JsonReader_isArray(pathArray)) continue;

        char** resolvedPaths = NULL;
        int pathCount = JsonReader_arrayLength(pathArray);
        for (int j = 0; j < pathCount; j++) {
            JsonValue* pathElement = JsonReader_getArrayElement(pathArray, j);
            if (!JsonReader_isString(pathElement)) continue;

            const char* rawPath = JsonReader_getString(pathElement);
            // On PS3 we don't have $BOOT: yet, so just copy the path
            arrput(resolvedPaths, safeStrdup(rawPath));
        }

        shput(pfs->mappings, gameFileName, resolvedPaths);
    }

    return (FileSystem*) pfs;
}

void Ps3FileSystem_destroy(FileSystem* fs) {
    Ps3FileSystem* pfs = (Ps3FileSystem*) fs;
    free(pfs->gameTitle);
    int mappingCount = shlen(pfs->mappings);
    for (int i = 0; i < mappingCount; i++) {
        char** paths = pfs->mappings[i].value;
        int pathCount = arrlen(paths);
        for (int j = 0; j < pathCount; j++) {
            free(paths[j]);
        }
        arrfree(paths);
    }
    shfree(pfs->mappings);
    free(pfs);
}
