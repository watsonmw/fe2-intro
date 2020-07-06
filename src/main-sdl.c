#include <stdio.h>
#include <SDL2/SDL.h>

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
static b32 sDumpFontModels = FALSE;
static const char* sFrontierExePath;
static RGB sFIntroPalette[256];

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

MReadFileRet LoadAmigaExe() {
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
            if (strcmp("mod", arg + 1) == 0) {
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
            } else if (strcmp("fullscreen", arg + 1) == 0) {
                sFullscreen = TRUE;
            } else if (strcmp("windowed", arg + 1) == 0) {
                sFullscreen = FALSE;
            } else if (strcmp("f", arg + 1) == 0) {
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
            } else if (strcmp("dump-intro-models", arg + 1) == 0) {
                sDumpIntroModels = TRUE;
            } else if (strcmp("dump-font-models", arg + 1) == 0) {
                sDumpFontModels = TRUE;
            }
        } else {
            sFrontierExePath = arg;
        }
    }
}

static void WriteAllModels(ModelsArray* modelsArray, u8* dataStart) {
    size_t i = 2;
    int foundNull = 0;
    MMemIO writer;
    MMemInitAlloc(&writer, 100);

    DebugModelParams params;
    memset(&params,  0, sizeof(DebugModelParams));
    params.maxSize = 0xfff;
    params.onlyLabels = 1;

    while (TRUE) {
        ModelData* modelData = MArrayGet(*modelsArray, i);
        if (modelData == NULL) {
            foundNull++;
            if (foundNull > 1) {
                break;
            }
        } else {
            foundNull = 0;
            u64 byteCodeBegin = ((u8*)modelData - dataStart) + modelData->codeOffset;
            DebugModelInfo modelInfo;
            params.offsetBegin = byteCodeBegin;

            DecompileModel(modelData, i, &params, &modelInfo, &writer);

            MArrayFree(modelInfo.modelIndexes);

            MLog((char*)writer.mem);
            writer.size = 0;
            writer.pos = writer.mem;
        }

        i++;
    }

    MMemFree(&writer);
}

int main(int argc, char**argv) {
    ParseCommandLine(argc, argv);

    // Load intro file data
    MReadFileRet amigaExe = LoadAmigaExe();
    if (amigaExe.size == 0) {
        printf("Unable to find Frontier amiga exe");
        return -1;
    }

    Surface surface;
    surface.pixels = 0;
    Surface_Init(&surface, SURFACE_WIDTH, SURFACE_HEIGHT);

    FMath_BuildLookupTables();

    RasterContext raster;
    raster.surface = &surface;
    raster.legacy = 0;
    Raster_Init(&raster);

    Intro intro;
    SceneSetup introSceneSetup;
    Render_Init(&introSceneSetup, &raster);
    Palette_SetupForNewFrame(&raster.paletteContext, TRUE);
    Palette_CopyFixedColoursRGB(&raster.paletteContext, sFIntroPalette);

    AssetsDataAmiga assetsDataAmiga;
    AssetsReadEnum assetsRead = AssetsRead_Amiga_EliteClub2;
    Assets_LoadAmigaFiles(&assetsDataAmiga, &amigaExe, assetsRead);
    Intro_InitAmiga(&intro, &introSceneSetup, &assetsDataAmiga);

    if (sDumpIntroModels) {
        WriteAllModels(&introSceneSetup.assets.models, assetsDataAmiga.mainExeData);
        return 0;
    }

    if (sDumpFontModels) {
        WriteAllModels(&introSceneSetup.assets.fontModels, assetsDataAmiga.mainExeData);
        return 0;
    }

    ModelsArray overrideModels;
    MArrayInit(overrideModels);
    MReadFileRet overridesFile = Assets_LoadModelOverrides("data/overrides-le.dat", &overrideModels);
    for (int i = 0; i < MArraySize(overrideModels); i++) {
        if (overrideModels.arr[i]) {
            MArraySet(introSceneSetup.assets.models, i, overrideModels.arr[i]);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    AudioContext audio;
    Audio_Init(&audio, assetsDataAmiga.mainExeData, assetsDataAmiga.mainExeSize);

    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    int windowWidth = 1600;
    int windowHeight = 900;
    SDL_Window* window = SDL_CreateWindow("Frontier Elite II - Intro",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, windowFlags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
            SURFACE_WIDTH, SURFACE_HEIGHT);
    SceneSetup* sceneSetup = &introSceneSetup;

    sClockTickInterval = SDL_GetPerformanceFrequency();
    Audio_ModStart(&audio, sModToPlay);

    u32 numFrames = Intro_GetNumFrames(&intro);

    SDL_ShowCursor(SDL_DISABLE);
    if (sFullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    const int speed1 = 10;
    const int speed2 = 100;

    int fpsFrames = 0;
    int fps = 0;
    u64 updateFpsTimer = 0;
    char frameRateString[128];

    u64 prevClock = SDL_GetPerformanceCounter();
    sStartTime = prevClock;

    b32 done = FALSE;
    while (!done) {
        fpsFrames++;

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    done = TRUE;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            done = TRUE;
                            break;
                        case SDLK_RETURN:
                            if (event.key.keysym.mod == KMOD_RALT || event.key.keysym.mod == KMOD_LALT) {
                                sFullscreen = !sFullscreen;
                                if (sFullscreen) {
                                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                                } else {
                                    SDL_SetWindowFullscreen(window, 0);
                                }
                            }
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
                        case SDLK_BACKQUOTE:
                            sDebugMode = !sDebugMode;
                            break;
                        case SDLK_SPACE:
                            sPause = !sPause;
                            if (sPause) {
                                Audio_ModStop(&audio);
                            } else {
                                u64 delta = Intro_GetTimeForFrameOffset(&intro, sFrameOffset);
                                sStartTime = sCurrentClock - (((delta + 1) * sClockTickInterval) / 100);
                                Audio_ModStartAt(&audio, sModToPlay, (delta + 1) / 2);
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_KEYUP:
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    done = TRUE;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                        event.window.windowID == SDL_GetWindowID(window)) {
                        done = TRUE;
                    }
                    break;
            }
        }

        sCurrentClock = SDL_GetPerformanceCounter();

        u64 elapsed = ((u64)(sCurrentClock - prevClock)) / (sClockTickInterval / 1000);
        prevClock = sCurrentClock;
        updateFpsTimer += elapsed;
        if (updateFpsTimer > 1000) {
            fps = fpsFrames - 1;
            fpsFrames = 0;
            updateFpsTimer = updateFpsTimer - 1000;
        }

        u64 deltaIn100thsOfaSecond = ((sCurrentClock - sStartTime) * 100) / sClockTickInterval;
        if (!sPause) {
            sFrameOffset = Intro_GetFrameOffsetAtTime(&intro, deltaIn100thsOfaSecond);
        }

        if (sFrameOffset >= numFrames) {
            // Restart intro
            sStartTime = sCurrentClock;
            sFrameOffset = 0;
            Audio_ModStart(&audio, sModToPlay);
        }

        if (Audio_ModDone(&audio)) {
            Audio_ModStart(&audio, Audio_ModEnum_SILENCE);
        }

        if (sFrameOffset >= numFrames) {
            sFrameOffset = 0;
            sStartTime = sCurrentClock;
            Audio_ModStart(&audio, sModToPlay);
        }

        if (!sPause || sRender) {
            RenderIntroAtTime(&intro, &introSceneSetup, sFrameOffset);
            sRender = FALSE;
        }

        if (sDebugMode && fps > 0) {
            snprintf(frameRateString, 100, "fps: %d frame: %d", fps, sFrameOffset);

            Vec2i16 pos = { 2, 2 };
            Render_DrawBitmapText(&introSceneSetup, frameRateString, pos, 0x7, TRUE);
        }

        int pitch;
        void* pixels;

        SDL_LockTexture(texture, NULL, &pixels, &pitch);

        UpdateSurfaceTexture(&surface, sFIntroPalette, (u8*)pixels, pitch);
        SDL_UnlockTexture(texture);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    Audio_Exit(&audio);
    Intro_Free(&intro, sceneSetup);
    Assets_FreeAmigaFiles(&assetsDataAmiga);
    MArrayFree(overrideModels);
    if (overridesFile.data) {
        MFree(overridesFile.data);
    }

    Render_Free(sceneSetup);
    Raster_Free(&raster);
    Surface_Free(&surface);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
