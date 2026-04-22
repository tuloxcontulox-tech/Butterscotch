#include "common.h"
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>

#include "runner.h"
#include "vm.h"
#include "data_win.h"
#include "json_reader.h"
#include "ps3_file_system.h"
#include "ps3_renderer.h"
#include "noop_audio_system.h"
#include "utils.h"

// Note: In a real PS3 SDK environment, you would include headers like:
// #include <sys/process.h>
// #include <io/pad.h>

static void pollPad(Runner* runner) {
    (void)runner;
    // TODO: Implement PS3 pad polling using io/pad.h or similar
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    printf("Butterscotch PS3 - Starting\n");

    const char* dataWinPath = "dev_hdd0/game/DELTARPS3/USRDIR/DATA.WIN";
    const char* configJsonPath = "dev_hdd0/game/DELTARPS3/USRDIR/CONFIG.JSN";

    // ===[ Load CONFIG.JSN ]===
    FILE* configFile = fopen(configJsonPath, "rb");
    JsonValue* configRoot = NULL;

    if (configFile != NULL) {
        fseek(configFile, 0, SEEK_END);
        long configSize = ftell(configFile);
        fseek(configFile, 0, SEEK_SET);

        char* configJsonText = safeMalloc((size_t) configSize + 1);
        size_t configBytesRead = fread(configJsonText, 1, (size_t) configSize, configFile);
        configJsonText[configBytesRead] = '\0';
        fclose(configFile);

        configRoot = JsonReader_parse(configJsonText);
        free(configJsonText);
    }

    if (configRoot == NULL) {
        printf("CONFIG.JSN invalid or not found!\n");
        return 1;
    }

    // ===[ Parse data.win ]===
    DataWin* dataWin = DataWin_parse(
        dataWinPath,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTpag = true,
            .parseCode = true,
            .parseStrg = true,
            // Add other options as needed
        }
    );

    if (dataWin == NULL) {
        printf("Failed to parse data.win!\n");
        return 1;
    }

    FileSystem* fileSystem = Ps3FileSystem_create(configRoot, dataWin->gen8.displayName);
    VMContext* vm = VM_create(dataWin);
    Renderer* renderer = Ps3Renderer_create();
    AudioSystem* audioSystem = (AudioSystem*) NoopAudioSystem_create();

    Runner* runner = Runner_create(dataWin, vm, renderer, fileSystem, audioSystem);
    Runner_initFirstRoom(runner);

    Gen8* gen8 = &dataWin->gen8;
    int32_t gameW = (int32_t) gen8->defaultWindowWidth;
    int32_t gameH = (int32_t) gen8->defaultWindowHeight;

    // ===[ Main Loop ]===
    while (!runner->shouldExit) {
        pollPad(runner);

        Runner_step(runner);

        renderer->vtable->beginFrame(renderer, gameW, gameH, 1280, 720); // Assuming 720p for PS3

        Runner_draw(runner);

        renderer->vtable->endFrame(renderer);

        // TODO: Implement proper frame timing/vsync for PS3
    }

    return 0;
}
