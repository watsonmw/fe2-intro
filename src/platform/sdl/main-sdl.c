#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <SDL2/SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include "audio.h"
#include "render.h"
#include "fintro.h"
#include "modelcode.h"

static b32 sRender = TRUE;
static b32 sFullscreen = FALSE;
static int sFrameOffset = 0;
static int sDebugMode = 0;
static int sPause = 0;

static u32 sClockTickInterval;
static u64 sStartTime;
static u64 sCurrentClock;

static u16 sModToPlay = Audio_ModEnum_FRONTIER_THEME_INTRO;
static b32 sDumpIntroModels = FALSE;
static b32 sDumpGameModels = FALSE;
static b32 sDumpGalmapModels = FALSE;
static const char* sFrontierExePath;
static RGB sFIntroPalette[256];

static MMemIO sModelOverrides;
static ModelsArray sOrigModels;
static const char* sFileToCompile = NULL;
static const char* sFileToHotCompile = NULL;

#define INTRO_OVERRIDES_LE "data/model-overrides-le.dat"
#define INTRO_OVERRIDES_BE "data/model-overrides-be.dat"

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

static void RenderIntroAtTime(Intro* intro, SceneSetup* sceneSetup, int frameOffset) {
    Intro_SetSceneForFrameOffset(intro, sceneSetup, frameOffset);

    Render_RenderAndDrawScene(sceneSetup, FALSE);

    Intro_Post3dRender(intro, sceneSetup, frameOffset);

    Palette_CopyDynamicColoursRGB(&sceneSetup->raster->paletteContext, (RGB*) sFIntroPalette);
}

static MReadFileRet LoadAmigaExe() {
    MReadFileRet amigaExeData;
    if (sFrontierExePath) {
        amigaExeData = MFileReadFully(sFrontierExePath);
        if (!amigaExeData.size) {
            MLogf("Unable to load Frontier exe '%s'", sFrontierExePath);
            exit(0);
        }
    } else {
        amigaExeData = MFileReadFully("game");
        if (!amigaExeData.size) {
            amigaExeData = MFileReadFully("frontier");
        }
    }
    return amigaExeData;
}

static i32 ParseCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (*arg == '-') {
            if (MStrCmp("mod", arg + 1) == 0) {
                i++;
                if (i >= argc) {
                    return -1;
                }
                const char* arg2 = argv[i];
                i32 mod = 0;
                if (!MParseI32(arg2, MStrEnd(arg2), &mod)) {
                    if (mod >= 0 && mod < 11) {
                        sModToPlay = mod;
                    }
                }
            } else if (MStrCmp("fullscreen", arg + 1) == 0) {
                sFullscreen = TRUE;
            } else if (MStrCmp("windowed", arg + 1) == 0) {
                sFullscreen = FALSE;
            } else if (MStrCmp("f", arg + 1) == 0) {
                i++;
                if (i >= argc) {
                    return -1;
                }
                const char* arg2 = argv[i];
                i32 offset = 0;
                if (!MParseI32(arg2, MStrEnd(arg2), &offset)) {
                    if (offset >= 0) {
                        sFrameOffset = offset;
                    }
                }
            } else if (MStrCmp("dump-intro-models", arg + 1) == 0) {
                sDumpIntroModels = TRUE;
            } else if (MStrCmp("dump-game-models", arg + 1) == 0) {
                sDumpGameModels = TRUE;
            } else if (MStrCmp("dump-galmap-models", arg + 1) == 0) {
                sDumpGalmapModels = TRUE;
            } else if (MStrCmp("compile", arg + 1) == 0) {
                i += 1;
                if (i >= argc) {
                    MLog("'-compile' option requires input file.");
                    MLogf("   %s -compile models.txt", argv[0]);
                    return -1;
                }
                sFileToCompile = argv[i];
            } else if (MStrCmp("hot-reload", arg + 1) == 0) {
                i += 1;
                if (i >= argc) {
                    MLog("'-hot-reload' option requires input file.");
                    MLogf("   %s -hot-reload models.txt", argv[0]);
                    return -1;
                }
                sFileToHotCompile = argv[i];
            }
        } else {
            sFrontierExePath = arg;
        }
    }

    return 0;
}

static void WriteAllModels(ModelsArray* modelsArray, const u8* dataStart) {
    i32 modelIndex = 0;
    i32 foundNull = 0;
    MMemIO writer;
    MMemInitAlloc(&writer, 100);

    DebugModelParams params;
    memset(&params,  0, sizeof(DebugModelParams));
    params.maxSize = 0xfff;
    params.onlyLabels = 1;

    while (TRUE) {
        ModelData* modelData = MArrayGet(*modelsArray, modelIndex);
        if (modelData == NULL) {
            if (modelIndex >= 2) {
                foundNull++;
                if (foundNull > 1) {
                    break;
                }
            }
        } else {
            foundNull = 0;
            u64 byteCodeBegin = ((u8*)modelData - dataStart) + modelData->codeOffset;
            DebugModelInfo modelInfo;
            params.offsetBegin = byteCodeBegin;

            if (modelIndex < 2) {
                FontModelData* fontModelData = (FontModelData*)modelData;
                DecompileFontModel(fontModelData, modelIndex, &params, &modelInfo, &writer);
            } else {
                DecompileModel(modelData, modelIndex, &params, &modelInfo, &writer);
            }

            MArrayFree(modelInfo.modelIndexes);

            MLog((char*)writer.mem);
            writer.size = 0;
            writer.pos = writer.mem;
        }

        modelIndex++;
    }

    MMemFree(&writer);
}

int CompileFileAndWriteOut(const char* fileToCompile, const char* fileOutputPath, MMemIO* memOutput,
                           ModelsArray* modelsArray, ModelEndianEnum endian, b32 dumpModelsToConsole) {

    MLogf("Compiling: %s", fileToCompile);

    int result = CompileAndWriteModels(fileToCompile, fileOutputPath,
                                       memOutput, modelsArray, endian,
                                       dumpModelsToConsole);

    return result;
}

void HotReload(SceneSetup *introSceneSetup, SceneSetup *sceneSetup) {
    MMemReset(&sModelOverrides);
    ModelsArray modelsArray;
    MArrayInit(modelsArray);

    CompileFileAndWriteOut(sFileToHotCompile, INTRO_OVERRIDES_LE, &sModelOverrides, &modelsArray,
                           ModelEndian_LITTLE, TRUE);

    MArrayCopy(sOrigModels, (*introSceneSetup).assets.models);
    for (int i = 0; i < MArraySize(modelsArray); i++) {
        if (modelsArray.arr[i] != NULL) {
            MArraySet(sceneSetup->assets.models, i, modelsArray.arr[i]);
        }
    }

    MArrayFree(modelsArray);
}

typedef struct LoopContext {
    // App done - exit
    b32 done;

    // Have to wait for interaction before we can play sounds
    b32 waitForInteraction;

    // Don't render if hidden
    b32 windowHidden;
    i32 windowWidth;
    i32 windowHeight;

    // FPS and intro time calculation
    int fps;
    int fpsFrames;
    int numIntroFrames;
    int updateFpsTimer;
    u64 prevClock;

    // Game logic
    AudioContext audio;
    Intro intro;
    SceneSetup introScene;
    SceneSetup* sceneSetup;
    AssetsDataAmiga* assetsDataAmiga;
    Surface surface;

    // SDL stuff
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
} LoopContext;

static LoopContext sLoopContext;

void PauseIntro(b32 pause) {
    if (pause) {
        Audio_ModStop(&sLoopContext.audio);
    } else {
        u64 deltaIn100thsOfaSecond = Intro_GetTimeForFrameOffset(&sLoopContext.intro, sFrameOffset);
        u64 currentClock = SDL_GetPerformanceCounter();
        sStartTime = currentClock - (((deltaIn100thsOfaSecond + 1ul) * (u64)sClockTickInterval) / 100ul);
        Audio_ModStartAt(&sLoopContext.audio, sModToPlay, (deltaIn100thsOfaSecond + 1) / 2);
    }
    sPause = pause;
}

void StartIntro() {
    Audio_Init(&sLoopContext.audio,
               sLoopContext.assetsDataAmiga->mainExeData,
               sLoopContext.assetsDataAmiga->mainExeSize);

    sLoopContext.prevClock = SDL_GetPerformanceCounter();
    sStartTime = sLoopContext.prevClock;
    Audio_ModStart(&sLoopContext.audio, sModToPlay);
    Audio_SetVolume(&sLoopContext.audio, 128);
}

void MainLoopIteration() {
    const int speed1 = 10;
    const int speed2 = 100;

    sLoopContext.fpsFrames++;

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                sLoopContext.done = TRUE;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_BACKQUOTE:
                        sDebugMode = !sDebugMode;
                        break;
#ifdef DEBUG
                    case SDLK_ESCAPE:
                        sLoopContext.done = TRUE;
                        break;
                    case SDLK_RETURN:
#ifndef __EMSCRIPTEN__
                        if (event.key.keysym.mod == KMOD_RALT || event.key.keysym.mod == KMOD_LALT) {
                            sFullscreen = !sFullscreen;
                            if (sFullscreen) {
                                SDL_SetWindowFullscreen(sLoopContext.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                            } else {
                                SDL_SetWindowFullscreen(sLoopContext.window, 0);
                            }
                        }
#endif
                        break;
                    case SDLK_LEFT:
                        sRender = TRUE;
                        if (event.key.keysym.mod == KMOD_SHIFT) {
                            sFrameOffset -= speed1;
                        } else {
                            sFrameOffset -= 1;
                        }
                        break;
                    case SDLK_RIGHT:
                        sRender = TRUE;
                        if (event.key.keysym.mod == KMOD_SHIFT) {
                            sFrameOffset += speed1;
                        } else {
                            sFrameOffset += 1;
                        }
                        break;
                    case SDLK_UP:
                        sRender = TRUE;
                        if (event.key.keysym.mod == KMOD_SHIFT) {
                            sFrameOffset += speed2;
                        } else {
                            sFrameOffset += speed1;
                        }
                        break;
                    case SDLK_DOWN:
                        sRender = TRUE;
                        if (event.key.keysym.mod == KMOD_SHIFT) {
                            sFrameOffset -= speed2;
                        } else {
                            sFrameOffset -= speed1;
                        }
                        break;
                    case SDLK_SPACE:
                        sPause = !sPause;
                        PauseIntro(sPause);
                        break;
#endif
                    default:
                        break;
                }
                break;
            case SDL_KEYUP:
#ifdef __EMSCRIPTEN__
                switch (event.key.keysym.sym) {
                    case SDLK_SPACE:
                    case SDLK_KP_ENTER:
                    case SDLK_RETURN:
                        if (sLoopContext.waitForInteraction) {
                            sLoopContext.waitForInteraction = FALSE;
                            StartIntro();
                        }
                        break;
                }
#endif
                break;
            case SDL_MOUSEBUTTONDOWN:
#ifndef __EMSCRIPTEN__
                sLoopContext.done = TRUE;
#endif
                break;
            case SDL_MOUSEBUTTONUP:
#ifdef __EMSCRIPTEN__
                if (sLoopContext.waitForInteraction) {
                    sLoopContext.waitForInteraction = FALSE;
                    StartIntro();
                }
#endif
                break;
            case SDL_WINDOWEVENT:
                if (event.window.windowID == SDL_GetWindowID(sLoopContext.window)) {
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        sLoopContext.done = TRUE;
                    } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                        if (!sLoopContext.waitForInteraction) {
                            PauseIntro(FALSE);
                        }
                        sLoopContext.windowHidden = FALSE;
                        if (sFileToHotCompile) {
                            HotReload(&sLoopContext.introScene, sLoopContext.sceneSetup);
                            sRender = TRUE;
                        }
                    } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        if (!sLoopContext.waitForInteraction) {
                            PauseIntro(TRUE);
                        }
                        sLoopContext.windowHidden = TRUE;
                    }
                    break;
                }
                break;
        }
    }

    if (sLoopContext.windowHidden) {
        return;
    }

    sCurrentClock = SDL_GetPerformanceCounter();

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
        u64 elapsed = ((u64)(sCurrentClock - sLoopContext.prevClock)) / (sClockTickInterval / 1000);
        sLoopContext.prevClock = sCurrentClock;
        sLoopContext.updateFpsTimer += elapsed;
        if (sLoopContext.updateFpsTimer > 1000) {
            sLoopContext.fps = sLoopContext.fpsFrames - 1;
            sLoopContext.fpsFrames = 0;
            sLoopContext.updateFpsTimer = sLoopContext.updateFpsTimer - 1000;
        }

        u64 deltaIn100thsOfaSecond = ((sCurrentClock - sStartTime) * 100) / sClockTickInterval;
        if (!sPause) {
            sFrameOffset = Intro_GetFrameOffsetAtTime(&sLoopContext.intro, deltaIn100thsOfaSecond);
        }

        if (sFrameOffset >= sLoopContext.numIntroFrames) {
            // Restart intro
            sStartTime = sCurrentClock;
            Audio_ClearCache(&sLoopContext.audio);
            sFrameOffset = 0;
            Audio_ModStart(&sLoopContext.audio, sModToPlay);
        }

        if (Audio_ModDone(&sLoopContext.audio)) {
            Audio_ModStart(&sLoopContext.audio, Audio_ModEnum_SILENCE);
        }

        if (!sPause || sRender) {
            RenderIntroAtTime(&sLoopContext.intro, &sLoopContext.introScene, sFrameOffset);
            sRender = FALSE;
        }

        if (sDebugMode) {
            char frameRateString[128];
            snprintf(frameRateString, sizeof(frameRateString), "fps: %d frame: %d", sLoopContext.fps, sFrameOffset);

            Vec2i16 pos = { 2, 2 };
            Render_DrawBitmapText(&sLoopContext.introScene, frameRateString, pos, 0x7, TRUE);
        }
    }

    int pitch;
    void* pixels;

    SDL_LockTexture(sLoopContext.texture, NULL, &pixels, &pitch);

    UpdateSurfaceTexture(&sLoopContext.surface, sFIntroPalette, (u8*)pixels, pitch);
    SDL_UnlockTexture(sLoopContext.texture);

    SDL_RenderClear(sLoopContext.renderer);
    SDL_RenderCopy(sLoopContext.renderer, sLoopContext.texture, NULL, NULL);
    SDL_RenderPresent(sLoopContext.renderer);

#ifdef __EMSCRIPTEN__
    if (sLoopContext.done) {
        emscripten_cancel_main_loop();
        Audio_Exit(&sLoopContext.audio);
    }
#endif
}

int main(int argc, char**argv) {
    int result = ParseCommandLine(argc, argv);
    if (result) {
        return result;
    }

    if (sFileToCompile) {
        MMemIO memOutput;
        MMemInitAlloc(&memOutput, 1024);
        ModelsArray modelsArray;
        MArrayInit(modelsArray);

        result = CompileFileAndWriteOut(sFileToCompile, INTRO_OVERRIDES_LE, &memOutput, &modelsArray,
                                        ModelEndian_LITTLE, TRUE);


        MArrayClear(modelsArray);
        MMemReset(&memOutput);

        result = CompileFileAndWriteOut(sFileToCompile, INTRO_OVERRIDES_BE, &memOutput, &modelsArray,
                                        ModelEndian_BIG, FALSE);

        MArrayFree(modelsArray);
        MMemFree(&memOutput);

        return result;
    }

    // Load intro file data
    MReadFileRet amigaExe = LoadAmigaExe();
    if (amigaExe.size == 0) {
        printf("Unable to find Frontier amiga exe\n");
        return -1;
    }

    sLoopContext.surface.pixels = 0;
    Surface_Init(&sLoopContext.surface, SURFACE_WIDTH, SURFACE_HEIGHT);

    FMath_BuildLookupTables();

    RasterContext raster;
    raster.surface = &sLoopContext.surface;
    raster.legacy = 0;
    Raster_Init(&raster);

    Render_Init(&sLoopContext.introScene, &raster);
    Palette_SetupForNewFrame(&raster.paletteContext, TRUE);
    Palette_CopyFixedColoursRGB(&raster.paletteContext, sFIntroPalette);

    AssetsDataAmiga assetsDataAmiga;
    AssetsReadEnum assetsRead = AssetsRead_Amiga_EliteClub2;
    Assets_LoadAmigaFiles(&assetsDataAmiga, &amigaExe, assetsRead);
    sLoopContext.intro.drawFrontierLogo = 1;
    Intro_InitAmiga(&sLoopContext.intro, &sLoopContext.introScene, &assetsDataAmiga);

    if (sDumpIntroModels) {
        WriteAllModels(&sLoopContext.introScene.assets.models, assetsDataAmiga.mainExeData);
        return 0;
    } else if (sDumpGameModels) {
        ModelsArray mainModels;

        Assets_LoadModelPointers32BE(amigaExe.data + 0x28804, 300, &mainModels);

        // Flip model words
        ARRAY_REWRITE_BE16(amigaExe.data + 0x28c8c, 0x11698);

        // Flip back planet byte code
        // ARRAY_REWRITE_BE16(fileData + 0x28804, 28);

        WriteAllModels(&mainModels, assetsDataAmiga.mainExeData);
        return 0;
    } else if (sDumpGalmapModels) {
        WriteAllModels(&sLoopContext.introScene.assets.galmapModels, assetsDataAmiga.mainExeData);
        return 0;
    }

    MArrayCopy(sLoopContext.introScene.assets.models, sOrigModels);
    ModelsArray overrideModels;
    MArrayInit(overrideModels);
    MReadFileRet overridesFile = Assets_LoadModelOverrides(INTRO_OVERRIDES_LE, &overrideModels);
    for (int i = 0; i < MArraySize(overrideModels); i++) {
        if (overrideModels.arr[i]) {
            MArraySet(sLoopContext.introScene.assets.models, i, overrideModels.arr[i]);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        MLogf("SDL Init Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);

#ifdef __EMSCRIPTEN__
    // Make a window the surface size, let the browser scale up the canvas
    SDL_WindowFlags windowFlags = SDL_WINDOW_SHOWN;
    int windowWidth = SURFACE_WIDTH;
    int windowHeight = SURFACE_HEIGHT;
#else
    SDL_WindowFlags windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    int windowWidth = 1400;
    int windowHeight = 800;
#endif
    sLoopContext.window = SDL_CreateWindow("Frontier Elite II - Intro",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, windowFlags);
    sLoopContext.renderer = SDL_CreateRenderer(sLoopContext.window, -1,
                                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (sLoopContext.renderer == NULL) {
        SDL_DestroyWindow(sLoopContext.window);
        SDL_Quit();
        return 1;
    }

    sLoopContext.windowWidth = windowWidth;
    sLoopContext.windowHeight = windowHeight;

    sLoopContext.texture = SDL_CreateTexture(sLoopContext.renderer, SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_STREAMING,
            SURFACE_WIDTH, SURFACE_HEIGHT);
    SceneSetup* sceneSetup = &sLoopContext.introScene;

    sClockTickInterval = SDL_GetPerformanceFrequency();

#ifndef __EMSCRIPTEN__
    SDL_ShowCursor(SDL_DISABLE);
    if (sFullscreen) {
        SDL_SetWindowFullscreen(sLoopContext.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
#endif

    sLoopContext.done = FALSE;
#ifdef __EMSCRIPTEN__
    sLoopContext.waitForInteraction = TRUE;
#else
    sLoopContext.waitForInteraction = FALSE;
#endif
    sLoopContext.fps = 0;
    sLoopContext.fpsFrames = 0;
    sLoopContext.numIntroFrames = Intro_GetNumFrames(&sLoopContext.intro);
    sLoopContext.prevClock = SDL_GetPerformanceCounter();
    sLoopContext.sceneSetup = sceneSetup;
    sLoopContext.assetsDataAmiga = &assetsDataAmiga;

    if (!sLoopContext.waitForInteraction) {
        StartIntro();
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainLoopIteration, 0, TRUE);
#else
    while (!sLoopContext.done) {
        MainLoopIteration();
    }
#endif

    Audio_Exit(&sLoopContext.audio);
    Intro_Free(&sLoopContext.intro, sceneSetup);
    Assets_FreeAmigaFiles(&assetsDataAmiga);
    MArrayFree(overrideModels);
    if (overridesFile.data) {
        MFree(overridesFile.data, overridesFile.size);
    }

    MArrayFree(sOrigModels);

    Render_Free(sceneSetup);
    Raster_Free(&raster);
    Surface_Free(&sLoopContext.surface);

    SDL_DestroyRenderer(sLoopContext.renderer);
    SDL_DestroyWindow(sLoopContext.window);
    SDL_Quit();

    return 0;
}
