#include "assets.h"
#include "audio.h"
#include "fintro.h"
#include "mlib.h"
#include "render.h"
#include "platform/basicalloc.h"

typedef struct sWASM_Surface {
    u32 width;
    u32 height;
    u32* rgba;
} WASM_Surface;

typedef struct sWASM_AudioBuffer {
    u32 size;
    float* bufferLeft;
    float* bufferRight;
} WASM_AudioBuffer;

static const i32 sAudioAvailableMods[] = {
    Audio_ModEnum_FRONTIER_THEME_INTRO,
    Audio_ModEnum_HALL_OF_THE_MOUNTAIN_KING,
    Audio_ModEnum_BABA_YAGA,
    Audio_ModEnum_NIGHT_ON_THE_BARE_MOUNTAIN,
    Audio_ModEnum_FRONTIER_SECOND_THEME,
    Audio_ModEnum_RIDE_OF_THE_VALKYRIES,
    Audio_ModEnum_GREAT_GATES_OF_KIEV,
};

typedef struct LoopContext {
    MReadFileRet gameExeFile;
    MReadFileRet overridesFile;

    u16 audioModIndex;

    // App done - exit
    b32 done;

    // Have to wait for interaction before we can play sounds
    b32 waitForInteraction;

    // FPS and intro time calculation
    int fps;
    int fpsFrames;
    int numIntroFrames;
    int updateFpsTimer;
    u32 prevClock;

    // Game logic
    AudioContext audio;
    Intro intro;
    RasterContext raster;
    SceneSetup introScene;
    RenderEntity entity;
    AssetsDataAmiga assetsDataAmiga;
    ModelsArray overrideModels;
    Surface surface;
    u32* rgbaOutput;
    b32 debugMode;
    b32 render;
    u32 startTime;
    int frameOffset;
    int pause;
    WASM_Surface wasmSurface;
    WASM_AudioBuffer audioBuffer;
    RGB palette[256];
    MBasicAlloc* allocator;
} LoopContext;

static LoopContext sLoopContext;

static void StartIntro(u32 currentTimestampMs) {
    Audio_Init(&sLoopContext.audio,
               sLoopContext.assetsDataAmiga.mainExeData,
               sLoopContext.assetsDataAmiga.mainExeSize);

    sLoopContext.prevClock = currentTimestampMs;
    sLoopContext.startTime = sLoopContext.prevClock;
    Audio_ModStart(&sLoopContext.audio, sAudioAvailableMods[sLoopContext.audioModIndex]);
}

static void UpdateSurfaceTexture(Surface* surface, RGB* palette, u8* pixels, int pitch) {
    for (int y = 0; y < surface->height; ++y) {
        for (int x = 0; x < surface->width; ++x) {
            u8 index = surface->pixels[x + (y * surface->width)];
            RGB col = palette[index];

            int d = (x * 4) + (y *  pitch);
            pixels[d] = 0xff;
            pixels[d+1] = col.b;
            pixels[d+2] = col.g;
            pixels[d+3] = col.r;
        }
    }
}

static void RenderIntroAtTime(Intro* intro, SceneSetup* sceneSetup, RenderEntity* entity, int frameOffset) {
    Intro_SetSceneForFrameOffset(intro, sceneSetup, entity, frameOffset);

    Render_RenderAndDrawScene(sceneSetup, entity, FALSE);

    Intro_Post3dRender(intro, sceneSetup, frameOffset);

    Palette_CopyDynamicColoursRGB(&sceneSetup->raster->paletteContext, (RGB*)sLoopContext.palette);
}

__attribute__((export_name("set_paused")))
void set_paused(b32 pause, u32 currentTimestampMs) {
    if (!pause) {
        u64 deltaIn100thsOfaSecond = Intro_GetTimeForFrameOffset(&sLoopContext.intro, sLoopContext.frameOffset);
        sLoopContext.startTime = currentTimestampMs - ((deltaIn100thsOfaSecond + 1ul) * 10ul);
    }
    sLoopContext.pause = pause;
}

__attribute__((export_name("set_debug_mode")))
void set_debug_mode(b32 bDebugMode) {
    sLoopContext.debugMode = bDebugMode;
}

__attribute__((export_name("audio_mod_next")))
void audio_mod_next() {
    sLoopContext.audioModIndex++;
    i32 audioNum = (sizeof(sAudioAvailableMods) / sizeof(i32));
    if (sLoopContext.audioModIndex >= audioNum) {
        sLoopContext.audioModIndex = 0;
    }
    Audio_ModStart(&sLoopContext.audio, sAudioAvailableMods[sLoopContext.audioModIndex]);
}

__attribute__((export_name("audio_mod_prev")))
void audio_mod_prev() {
    sLoopContext.audioModIndex--;
    if (sLoopContext.audioModIndex < 0) {
        i32 audioNum = (sizeof(sAudioAvailableMods) / sizeof(i32));
        sLoopContext.audioModIndex = audioNum - 1;
    }
    Audio_ModStart(&sLoopContext.audio, sAudioAvailableMods[sLoopContext.audioModIndex]);
}

__attribute__((export_name("audio_volume_up")))
void audio_volume_up() {
    u8 volume = sLoopContext.audio.masterVolume + 16;
    if (volume > AUDIO_VOLUME_MAX) {
        volume = AUDIO_VOLUME_MAX;
    }
    Audio_SetVolume(&sLoopContext.audio, volume);
}

__attribute__((export_name("audio_volume_down")))
void audio_volume_down() {
    u8 volume = sLoopContext.audio.masterVolume;
    if (volume >= 16) {
        volume -= 16;
    }
    Audio_SetVolume(&sLoopContext.audio, volume);
}

extern i32 __heap_base;
#define WASM_BLOCK_SIZE 0x10000

static size_t HeapMemoryGrow(u8* mem, size_t oldSize, size_t newSize) {
    MLogf("HeapMemoryGrow (was %d bytes, requested %d bytes)", oldSize, newSize);
    if (newSize > oldSize) {
        size_t addBlocks = ((newSize - oldSize) + (WASM_BLOCK_SIZE - 1)) / WASM_BLOCK_SIZE;
        size_t addBytes = WASM_BLOCK_SIZE * addBlocks;
        MLogf("Growing memory by %d blocks (%d bytes)", addBlocks, addBytes);
        __builtin_wasm_memory_grow(0, (long) addBlocks);
        size_t size = __builtin_wasm_memory_size(0);
        MLogf("Total memory blocks allocated %d", size);
        return addBytes;
    } else {
        return 0;
    }
}

void Audio_ClearCache(AudioContext *pContext);

__attribute__((export_name("setup")))
int setup(void) {
    u8* wasm_heap_start = (u8*)&__heap_base;
    size_t wasm_heap_size = __builtin_wasm_memory_size(0) * WASM_BLOCK_SIZE - (wasm_heap_start - ((u8*)0));

    sLoopContext.allocator = MBasicAlloc_Init(wasm_heap_start, wasm_heap_size);
    sLoopContext.allocator->memoryGrowFunc = HeapMemoryGrow;
    MMemAllocSet(&sLoopContext.allocator->funcs);

    // Load intro file data
    sLoopContext.gameExeFile = MFileReadFully("game");
    if (sLoopContext.gameExeFile.size == 0) {
        MLog("Unable to find Frontier amiga exe");
        return -1;
    }

    sLoopContext.surface.pixels = 0;
    Surface_Init(&sLoopContext.surface, SURFACE_WIDTH, SURFACE_HEIGHT);

    FMath_BuildLookupTables();

    sLoopContext.raster.surface = &sLoopContext.surface;
    sLoopContext.raster.legacy = 0;
    Raster_Init(&sLoopContext.raster);

    Render_Init(&sLoopContext.introScene, &sLoopContext.raster);
    Palette_SetupForNewFrame(&sLoopContext.raster.paletteContext, TRUE);
    Palette_CopyFixedColoursRGB(&sLoopContext.raster.paletteContext, sLoopContext.palette);

    AssetsReadEnum assetsRead = AssetsRead_Amiga_EliteClub2;
    Assets_LoadAmigaFiles(&sLoopContext.assetsDataAmiga, &sLoopContext.gameExeFile, assetsRead);
    sLoopContext.intro.drawFrontierLogo = 0;
    Intro_InitAmiga(&sLoopContext.intro, &sLoopContext.introScene, &sLoopContext.entity, &sLoopContext.assetsDataAmiga);

    MArrayInit(sLoopContext.overrideModels);
    sLoopContext.overridesFile =
        Assets_LoadModelOverrides("model-overrides-le.dat", &sLoopContext.overrideModels);
    for (int i = 0; i < MArraySize(sLoopContext.overrideModels); i++) {
        if (sLoopContext.overrideModels.arr[i]) {
            MArraySet(sLoopContext.introScene.assets.models, i, sLoopContext.overrideModels.arr[i]);
        }
    }

    sLoopContext.render = TRUE;
    sLoopContext.done = FALSE;
    sLoopContext.waitForInteraction = TRUE;
    sLoopContext.fps = 0;
    sLoopContext.fpsFrames = 0;
    sLoopContext.numIntroFrames = Intro_GetNumFrames(&sLoopContext.intro);

    // Setup the RGA output surface - this will be shared with JavaScript
    sLoopContext.wasmSurface.width = sLoopContext.surface.width;
    sLoopContext.wasmSurface.height = sLoopContext.surface.height;
    sLoopContext.rgbaOutput = MMalloc(sLoopContext.wasmSurface.width * sLoopContext.wasmSurface.height * 4);
    sLoopContext.wasmSurface.rgba = sLoopContext.rgbaOutput;

    return 1;
}

__attribute__((export_name("cleanup")))
void cleanup(void) {
    Audio_Exit(&sLoopContext.audio);
//    Intro_Free(&sLoopContext.intro, entity);
//    Assets_FreeAmigaFiles(&assetsDataAmiga);
//    MArrayFree(overrideModels);
//    if (overridesFile.data) {
//        MFree(overridesFile.data, overridesFile.size);
//    }
//
//    MArrayFree(sOrigModels);
//
//    Render_Free(entity);
//    Raster_Free(&raster);
    Surface_Free(&sLoopContext.surface);
}

__attribute__((export_name("get_surface")))
WASM_Surface* get_surface() {
    return &sLoopContext.wasmSurface;
}

static void RenderToRGASurface() {
    u8* pixels = sLoopContext.surface.pixels;
    u32* rgbaOutput = sLoopContext.rgbaOutput;
    for (int y = 0; y < sLoopContext.wasmSurface.height; y++) {
        for (int x = 0; x < sLoopContext.wasmSurface.width; x++) {
            RGB* color = sLoopContext.palette + *pixels++;
            // WASM is little endian and since we are writing 32bits at time, swizzle to abgr
            u32 rgba = ((((((0xff << 8) + ((u32)color->b))
                                  << 8) + ((u32)color->g))
                                  << 8) + ((u32)color->r));
            *rgbaOutput++ = rgba;
        }
    }
}

__attribute__((export_name("start_intro_after_interaction")))
void start_intro_after_interaction(u32 currentTimestampMs) {
    if (sLoopContext.waitForInteraction) {
        sLoopContext.waitForInteraction = FALSE;
        StartIntro(currentTimestampMs);
    }
}

__attribute__((export_name("render")))
void render(u32 currentTimestampMs) {
    sLoopContext.fpsFrames++;

    if (sLoopContext.waitForInteraction) {
        Surface_Clear(&sLoopContext.surface, 0);

        Vec2i16 pos1 = { 125, 60 };
        Render_DrawBitmapText(&sLoopContext.introScene, "Frontier: Elite 2 Intro", pos1, 0xf, TRUE);

        Vec2i16 pos2 = { 138, 120 };
        static i32 textColor = 0;
        textColor = (textColor + 1) % 32;
        i32 colour = (FMath_sine[textColor << 6] >> 13) + 4;
        Render_DrawBitmapText(&sLoopContext.introScene, "Click To Start!", pos2, colour + 3, FALSE);
    } else {
        u32 elapsedMs = currentTimestampMs - sLoopContext.prevClock;
        sLoopContext.prevClock = currentTimestampMs;
        sLoopContext.updateFpsTimer += elapsedMs;
        if (sLoopContext.updateFpsTimer > 1000) {
            sLoopContext.fps = sLoopContext.fpsFrames - 1;
            sLoopContext.fpsFrames = 0;
            sLoopContext.updateFpsTimer = sLoopContext.updateFpsTimer - 1000;
        }

        u64 deltaIn100thsOfaSecond = ((currentTimestampMs - sLoopContext.startTime) / 10);

        if (!sLoopContext.pause) {
            sLoopContext.frameOffset = Intro_GetFrameOffsetAtTime(&sLoopContext.intro, deltaIn100thsOfaSecond);
        }

        if (sLoopContext.frameOffset >= sLoopContext.numIntroFrames) {
            // Restart intro
            sLoopContext.startTime = currentTimestampMs;
            sLoopContext.frameOffset = 0;
            Audio_ModStart(&sLoopContext.audio, sAudioAvailableMods[sLoopContext.audioModIndex]);

            MLog("Restarting intro");
            Audio_ClearCache(&sLoopContext.audio);
            MBasicAlloc_PrintStats(sLoopContext.allocator);
        }

        if (Audio_ModDone(&sLoopContext.audio)) {
            Audio_ModStart(&sLoopContext.audio, Audio_ModEnum_SILENCE);
        }

        if (!sLoopContext.pause || sLoopContext.render) {
            RenderIntroAtTime(&sLoopContext.intro, &sLoopContext.introScene, &sLoopContext.entity, sLoopContext.frameOffset);
            sLoopContext.render = FALSE;
        }

        if (sLoopContext.debugMode) {
            char frameRateString[128];
            snprintf(frameRateString, sizeof(frameRateString), "FPS: %d Frame: %d", sLoopContext.fps, sLoopContext.frameOffset);

            Vec2i16 pos = { 2, 2 };
            Render_DrawBitmapText(&sLoopContext.introScene, frameRateString, pos, 0x3, TRUE);
        }
    }
    RenderToRGASurface();
}

__attribute__((export_name("audio_render")))
WASM_AudioBuffer* audio_render(int ticks) {
    AudioContext* audio = &sLoopContext.audio;
    Audio_RenderFrames(audio, ticks);
    sLoopContext.audioBuffer.bufferLeft = audio->audioOutputBufferLeft;
    sLoopContext.audioBuffer.bufferRight = audio->audioOutputBufferRight;
    sLoopContext.audioBuffer.size = audio->audioOutputBufferSize / sizeof(*sLoopContext.audioBuffer.bufferLeft);
    return &sLoopContext.audioBuffer;
}
