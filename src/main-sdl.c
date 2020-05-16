#include <stdio.h>
#include <SDL2/SDL.h>

#include "audio.h"
#include "render.h"
#include "fintro.h"

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

static RGB FIntroPalette[256];

static void RenderIntroAtTime(Intro* intro, SceneSetup* sceneSetup, int frameOffset, b32 resetPalette) {
    Intro_SetSceneForFrameOffset(intro, sceneSetup, frameOffset);

    Render_RenderAndDrawScene(sceneSetup, resetPalette);

    Intro_Post3dRender(intro, sceneSetup, frameOffset);

    if (resetPalette) {
        Palette_CopyFixedColoursRGB(&sceneSetup->raster->paletteContext, (RGB*) FIntroPalette);
    }
    Palette_CopyDynamicColoursRGB(&sceneSetup->raster->paletteContext, (RGB*) FIntroPalette);
}

MReadFileRet LoadAmigaExe() {
    MReadFileRet amigaExeData = MFileReadFully("game");
    if (!amigaExeData.size) {
        amigaExeData = MFileReadFully("frontier");
    }
    return amigaExeData;
}

int main(int argc, char**argv) {
    AssetsReadEnum assetsRead = AssetsRead_Amiga_EliteClub2;

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

    AssetsDataAmiga assetsDataAmiga;

    // Load intro file data
    MReadFileRet amigaExe = LoadAmigaExe();
    if (amigaExe.size == 0) {
        printf("Unable to find Frontier amiga exe");
        return -1;
    }

    Assets_LoadAmigaFiles(&assetsDataAmiga, &amigaExe, assetsRead);
    Intro_InitAmiga(&intro, &introSceneSetup, &assetsDataAmiga);

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

    b32 fullscreen = FALSE;
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    SDL_Window* window = SDL_CreateWindow("Frontier Elite II - Intro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 900, windowFlags);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, SURFACE_WIDTH, SURFACE_HEIGHT);
    SceneSetup* sceneSetup = &introSceneSetup;

    u64 startTime = SDL_GetPerformanceCounter();
    u64 counterTicksPerSecond = SDL_GetPerformanceFrequency();
    Audio_ModStart(&audio, Audio_ModEnum_FRONTIER_THEME_INTRO);

    int frameOffset = 0;
    u32 numFrames = Intro_GetNumFrames(&intro);
    RenderIntroAtTime(&intro, sceneSetup, frameOffset, TRUE);

    SDL_ShowCursor(SDL_DISABLE);

    b32 done = FALSE;
    while (!done) {
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
                                if (fullscreen) {
                                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                                } else {
                                    SDL_SetWindowFullscreen(window, 0);
                                }
                                fullscreen = !fullscreen;
                            }
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

        u64 currentTime = SDL_GetPerformanceCounter();
        u64 deltaIn100thsOfaSecond = ((currentTime - startTime) * 100) / counterTicksPerSecond;
        frameOffset = Intro_GetFrameOffsetAtTime(&intro, deltaIn100thsOfaSecond);

        if (Audio_ModDone(&audio)) {
            Audio_ModStart(&audio, Audio_ModEnum_SILENCE);
        }

        if (frameOffset >= numFrames) {
            frameOffset = 0;
            startTime = currentTime;
            Audio_ModStart(&audio, Audio_ModEnum_FRONTIER_THEME_INTRO);
        }

        RenderIntroAtTime(&intro, sceneSetup, frameOffset, FALSE);

        int pitch;
        void* pixels;

        SDL_LockTexture(texture, NULL, &pixels, &pitch);

        UpdateSurfaceTexture(&surface, FIntroPalette, (u8*)pixels, pitch);
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
