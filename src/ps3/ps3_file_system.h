#pragma once

#include "common.h"
#include "../file_system.h"
#include "../json_reader.h"

// Creates a PS3 file system that maps game-relative file names to PS3 device paths
// using the "fileSystem" object from a parsed CONFIG.JSN root
//
// configRoot: parsed JSON root of CONFIG.JSN (caller retains ownership, not freed here)
// gameTitle: the game's display name
FileSystem* Ps3FileSystem_create(JsonValue* configRoot, const char* gameTitle);
void Ps3FileSystem_destroy(FileSystem* fs);
