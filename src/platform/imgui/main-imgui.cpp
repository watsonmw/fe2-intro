#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include "imgui_memory_editor.h"

#include <stdio.h>
#include <SDL2/SDL.h>

#include <GL/gl3w.h>

#include "mini.h"
#include "audio.h"
#include "render.h"
#include "fintro.h"
#include "annotation.h"
#include "drawinfo.h"
#include "modelcode.h"
#include "modelviewer.h"

#include <fstream>
#include <iostream>
#include "tinypng/TinyPngOut.hpp"


static RGB sDefaultPalette[256];
static bool sModelRendered = false;
static u8 sMinIndex = 0;
static u8 sMaxIndex = 0;
static u8* sSurfaceTexturePixels = NULL;


MINTERNAL void WritePng(const char* filepath, Surface* surface, RGB* palette) {
    std::ofstream out(filepath, std::ios::binary);
    TinyPngOut pngout(surface->width, surface->height, out);

    u8 minIndex = 0xff;
    u8 maxIndex = 0;

    u8* surfaceTexturePixels = (u8*) MMalloc(surface->width * surface->height * 3);

    for (int y = 0; y < surface->height; ++y) {
        for (int x = 0; x < surface->width; ++x) {
            u8 index = surface->pixels[x + (y * surface->width)];
            RGB col = palette[index];

            if (index != 0 && index < minIndex) {
                minIndex = index;
            }

            if (index > maxIndex) {
                maxIndex = index;
            }

            int d = (x + (y *  surface->width)) * 3;
            surfaceTexturePixels[d] = col.r;
            surfaceTexturePixels[d+1] = col.g;
            surfaceTexturePixels[d+2] = col.b;
        }
    }

    pngout.write(surfaceTexturePixels, static_cast<size_t>(surface->width * surface->height));

    MFree(surfaceTexturePixels, surface->width * surface->height * 3);
}

MINTERNAL MReadFileRet Assets_LoadAmigaExeFromDataDir(AssetsReadEnum assetsRead) {
    if (assetsRead == AssetsRead_Amiga_EliteClub) {
        return MFileReadFully("data/FrontierSE.amiga");
    } else if (assetsRead == AssetsRead_Amiga_EliteClub2) {
        return MFileReadFully("game");
    } else {
        return MFileReadFully("data/Frontier.amiga");
    }
}

MINTERNAL void Assets_LoadPCFiles(AssetsDataPC* assets) {
    MReadFileRet frontierExe = MFileReadFully("data/frontier.exe");
    assets->mainExeData = frontierExe.data;
    assets->mainExeSize = frontierExe.size;

    MReadFileRet introModuleData = MFileReadFully("data/el2m08.ovl");
    assets->introModuleData = introModuleData.data;
    assets->introModuleSize = introModuleData.size;

    MReadFileRet gameMenuModuleData = MFileReadFully("data/el2m10.ovl");
    assets->gameMenuModuleData = gameMenuModuleData.data;
    assets->gameMenuModuleSize = gameMenuModuleData.size;

    MReadFileRet videoModelData = MFileReadFully("data/el2mcga3.ovl");
    assets->videoModuleData = videoModelData.data;
    assets->videoModuleSize = videoModelData.size;

    assets->mainStringData = frontierExe.data + 0x1f73a;
    assets->galmapModels = frontierExe.data + 0x283e6; // size: 0x1654
    assets->bitmapFontData = videoModelData.data + 0x6164;
    assets->defaultPalette = videoModelData.data + 0x5fe7;
}

MINTERNAL GLuint CreateTextureForSurface(Surface *surface) {
    free(sSurfaceTexturePixels); sSurfaceTexturePixels = NULL;

    sSurfaceTexturePixels = (uint8_t*) malloc(surface->width * surface->height * 4);

    // Turn the RGBA pixel data into an OpenGL texture:
    GLuint glTextureId;
    glGenTextures(1, &glTextureId);
    glBindTexture(GL_TEXTURE_2D, glTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    return glTextureId;
}

MINTERNAL void UpdateSurfaceTexture(GLuint glTextureId, Surface* surface, RGB* palette) {
    for (int y = 0; y < surface->height; ++y) {
        for (int x = 0; x < surface->width; ++x) {
            u8 index = surface->pixels[x + (y * surface->width)];
            RGB col = palette[index];

            if (index != 0 && index < sMinIndex) {
                sMinIndex = index;
            }

            if (index > sMaxIndex) {
                sMaxIndex = index;
            }

            int d = (x + (y *  surface->width)) * 4;
            sSurfaceTexturePixels[d] = col.r;
            sSurfaceTexturePixels[d + 1] = col.g;
            sSurfaceTexturePixels[d + 2] = col.b;
            sSurfaceTexturePixels[d + 3] = 0xff;
        }
    }

    glBindTexture(GL_TEXTURE_2D, glTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->width, surface->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, sSurfaceTexturePixels);
}

MINTERNAL void RenderIntroAtTime(GLuint surfaceTexture, Surface* surface, Intro* intro, SceneSetup* sceneSetup,
                                 RenderEntity* entity, int frameOffset, bool* resetPalette) {
    sModelRendered = true;

    Intro_SetSceneForFrameOffset(intro, sceneSetup, entity, frameOffset);

    Render_RenderAndDrawScene(sceneSetup, entity,  *resetPalette);

    Intro_Post3dRender(intro, sceneSetup, frameOffset);

    if (*resetPalette) {
        Palette_CopyFixedColoursRGB(&sceneSetup->raster->paletteContext, (RGB*) sDefaultPalette);
        *resetPalette = false;
    }
    Palette_CopyDynamicColoursRGB(&sceneSetup->raster->paletteContext, (RGB*) sDefaultPalette);
    UpdateSurfaceTexture(surfaceTexture, surface, (RGB *) sDefaultPalette);
}

MINTERNAL void RenderModelViewer(GLuint surfaceTexture, Surface* surface, ModelViewer* modelViewer, u16 modelIndex,
                                 bool resetPalette) {
    sModelRendered = true;

    if (modelViewer->modelIndex != modelIndex) {
        ModelViewer_SetModelForScene(modelViewer, modelIndex);
    }

    if (ModelViewer_UpdateScene(modelViewer)) {
        Render_RenderAndDrawScene(&(modelViewer->sceneSetup), &(modelViewer->entity), resetPalette);
        if (resetPalette) {
            Palette_CopyFixedColoursRGB(&modelViewer->sceneSetup.raster->paletteContext, (RGB*) sDefaultPalette);
        }
        Palette_CopyDynamicColoursRGB(&modelViewer->sceneSetup.raster->paletteContext, (RGB*) sDefaultPalette);
        UpdateSurfaceTexture(surfaceTexture, surface, (RGB *) sDefaultPalette);
    } else {
        Surface_Clear(modelViewer->sceneSetup.raster->surface, 0x7f);
    }
}

void WriteAllModels(const char* modelsFilename, Annotations* annotations, SceneSetup* sceneSetup) {
    size_t i = 2;
    int foundNull = 0;
    MMemIO writer;
    MMemInitAlloc(&writer, 100);

    DebugModelParams params;
    memset(&params,  0, sizeof(DebugModelParams));
    params.maxSize = 0xfff;
    params.codeOffsets = 1;
    params.byteCodeTrace = &sceneSetup->debug.byteCodeTrace;

    while (true) {
        ModelData* modelData = Render_GetModel(sceneSetup, i);
        if (modelData == NULL) {
            foundNull++;
            if (foundNull > 1) {
                break;
            }
        } else {
            foundNull = 0;
            u64 byteCodeBegin = ((u8*)modelData - sceneSetup->debug.modelDataFileStartAddress) + modelData->codeOffset;
            DebugModelInfo modelInfo;
            params.offsetBegin = byteCodeBegin;

            DecompileModel(modelData, i, &params, &modelInfo, &writer);

            MArrayFree(modelInfo.modelIndexes);

            MStringAppend(&writer, "\n\n");
        }

        i++;
    }
    MFileWriteDataFully(modelsFilename, writer.mem, writer.size);

    MMemFree(&writer);
}

struct OverridesFile {
    bool override = false;
    MReadFileRet fileData{};
    MMemIO memOutput{}; // where compiled model code is stored
    MFileInfo fieInfo{};
};

MINTERNAL void FreeModelOverridesFile(OverridesFile* overridesFile) {
    if (overridesFile->fileData.data) {
        MFree(overridesFile->fileData.data, overridesFile->fileData.size);
        overridesFile->fileData.data = 0;
        overridesFile->fileData.size = 0;
    }
    if (overridesFile->memOutput.mem) {
        MMemFree(&overridesFile->memOutput); overridesFile->memOutput.mem = 0;
    }
}

i32 LoadModelOverridesIfChanged(const char* filename, SceneSetup* sceneSetup, OverridesFile* overridesFile) {
    u64 lastModifiedTime = overridesFile->fieInfo.lastModifiedTime;

    overridesFile->fieInfo = MGetFileInfo(filename);
    if (!overridesFile->fieInfo.exists) {
        return -1;
    }

    if (overridesFile->fieInfo.lastModifiedTime > lastModifiedTime) {
        FreeModelOverridesFile(overridesFile);
        overridesFile->fileData = MFileReadFully(filename);
        if (overridesFile->fileData.size == 0) {
            return -1;
        }

        MMemInitAlloc(&overridesFile->memOutput, 1024);
        ModelCompileResult result = CompileMultipleModels(
                (const char*)overridesFile->fileData.data, overridesFile->fileData.size,
                &overridesFile->memOutput,&sceneSetup->assets.models,ModelEndian_LITTLE,
                TRUE);

        if (result.error) {
            MLogf("Got error: %s", result.error);
            MLogf("    at line: %d, column: %d", result.errorLine, result.errorColumn);
            if (!result.staticError) {
                MFree(result.error, result.errorLen); result.error = NULL;
            }
            FreeModelOverridesFile(overridesFile);
            return -2;
        } else {
            return 0;
        }
    }

    return -1;
}

MINTERNAL int CountModels(SceneSetup* sceneSetup) {
    int foundNull;
    int i = 0;
    while (true) {
        u32 modelOffset2 = Render_GetModelCodeOffset(sceneSetup, i);
        if (modelOffset2 == 0) {
            foundNull++;
            if (foundNull > 1 && i > 2) {
                break;
            }
        } else {
            foundNull = 0;
        }
        i++;
    }
    return i-1;
}

int main(int, char**) {
    MMemDebugInit();

//    Float16 f1 = Float16MakeFromInt(-0x3000);
//    Float16 f2 = Float16MakeFromInt(0x7fff);
//    MLogf("%e", Float16ieee(f1));
//    MLogf("%e", Float16ieee(f2));
//
//    Float32 f3 = Float16Mult32(f1, f2);
//    MLogf("%e", Float32ieee(f3));
//
//    Float16 f4 = Float16Mult(f1, f2);
//    MLogf("%e", Float16ieee(f4));

    AssetsReadEnum assetsRead = AssetsRead_Amiga_EliteClub2;

    Surface surface;
    surface.pixels = NULL;
    surface.insOffset = NULL;
    Surface_Init(&surface, SURFACE_WIDTH, SURFACE_HEIGHT);

    FMath_BuildLookupTables();

    RGB fileRasterPalette[256];
    RasterContext fileRasterContext;
    fileRasterContext.surface = &surface;
    fileRasterContext.legacy = 1;
    Raster_Init(&fileRasterContext);
    Palette_SetupForNewFrame(&fileRasterContext.paletteContext, true);

    RasterContext raster;
    PaletteContext* paletteContext = &raster.paletteContext;
    raster.surface = &surface;
    raster.legacy = 0;
    Raster_Init(&raster);

    Intro intro;
    SceneSetup introSceneSetup{};
    RenderEntity introEntity;
    Render_Init(&introSceneSetup, &raster);

    // Enabled debug render tracing
    introSceneSetup.debug.logLevel = 1;
    MArrayInit(introSceneSetup.debug.loadedModelIndexes);
    MArrayInit(introSceneSetup.debug.byteCodeTrace);
    memset(introSceneSetup.debug.hideModel, 0, sizeof(introSceneSetup.debug.hideModel));

    // Setup raster
    Palette_SetupForNewFrame(&raster.paletteContext, true);

    AssetsDataPC assetsDataPc;

    AssetsDataAmiga assetsDataAmiga;
    ModelViewer modelViewer{};

    // Load intro file data
    if (assetsRead == AssetsRead_PC_EliteClub) {
        Assets_LoadPCFiles(&assetsDataPc);
        Intro_InitPC(&intro, &introSceneSetup, &assetsDataPc);
        ModelViewer_InitPC(&modelViewer, &assetsDataPc);

        // Also load amiga version for audio
        MReadFileRet amigaExe = Assets_LoadAmigaExeFromDataDir(AssetsRead_Amiga_EliteClub);
        Assets_LoadAmigaFiles(&assetsDataAmiga, &amigaExe, AssetsRead_Amiga_EliteClub);
    } else {
        MReadFileRet amigaExe = Assets_LoadAmigaExeFromDataDir(assetsRead);
        Assets_LoadAmigaFiles(&assetsDataAmiga, &amigaExe, assetsRead);

        Intro_InitAmiga(&intro, &introSceneSetup, &assetsDataAmiga);
        ModelViewer_InitAmiga(&modelViewer, &assetsDataAmiga);
    }

    Render_Init(&modelViewer.sceneSetup, &raster);

    // Enabled debug render tracing
    modelViewer.sceneSetup.debug.logLevel = 1;
    MArrayInit(modelViewer.sceneSetup.debug.loadedModelIndexes);
    MArrayInit(modelViewer.sceneSetup.debug.byteCodeTrace);
    memset(modelViewer.sceneSetup.debug.hideModel, 0, sizeof(modelViewer.sceneSetup.debug.hideModel));

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_AUDIO) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    AudioContext audio;
    Audio_Init(&audio, assetsDataAmiga.mainExeData, assetsDataAmiga.mainExeSize);
    modelViewer.sceneSetup.audio = &audio;
    introSceneSetup.audio = &audio;

    // Setup Dear ImGui context
    // Decide GL+GLSL versions
#if __APPLE__
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("FE2 Intro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 2250, 1300, window_flags);
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
    bool err = gl3wInit() != 0;
    if (err) {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    GLuint surfaceTexture = CreateTextureForSurface(&surface);
    UpdateSurfaceTexture(surfaceTexture, &surface, (RGB *)sDefaultPalette);

    SceneSetup* curSceneSetup = &introSceneSetup;
    RenderEntity* curEntity = &introEntity;
    DebugModelParams debugModelParams{};
    int imageOffset = 0;
    int frameOffset = 0;
    MemoryEditor hexEditor;
    int sceneTab = 0;
    int modelTab = 0;
    int modelRadio = 0;
    int drawTab = 0;
    int modOffset = 1;
    int volume = 32;
    int sampleVolume = 63;
    int sampleId = 5;
    int engineSoundId = 1;
    int tonePeriod = 1000;
    int selectedModel = 0;
    int playIntro = 0;
    size_t numModels = 0;
    const int textInputBuffSize = 1024;
    char textInputBuff[textInputBuffSize];
    Annotations annotations;
    bool renderScene = true;
    int modelOffset = 11;
    const char *annotationsFile = "data/annotations.csv";
    annotations.load(annotationsFile);
    u32 introNumFrames = Intro_GetNumFrames(&intro) - 1;
    int showEntityHexView = 0;
    ScenePos scenePos{};

    u64 startTime = SDL_GetPerformanceCounter();
    u64 lastFileCheckTime = startTime;
    u64 tick = SDL_GetPerformanceFrequency();

    MIni ini;
    if (MIniLoadFile(&ini, "persist.ini")) {
        MIniReadI32(&ini, "frameOffset", &frameOffset);
        MIniReadI32(&ini, "modelOffset", &modelOffset);
        MIniReadI32(&ini, "modelViewer.pitch", &modelViewer.pitch);
        MIniReadI32(&ini, "modelViewer.yaw", &modelViewer.yaw);
        MIniReadI32(&ini, "modelViewer.roll", &modelViewer.roll);
        MIniReadI32(&ini, "modelViewer.x", &modelViewer.pos[0]);
        MIniReadI32(&ini, "modelViewer.y", &modelViewer.pos[1]);
        MIniReadI32(&ini, "modelViewer.z", &modelViewer.pos[2]);
        MIniReadI32(&ini, "modelViewer.lightingAngleA", &modelViewer.lightingAngleA);
        MIniReadI32(&ini, "modelViewer.lightingAngleB", &modelViewer.lightingAngleB);
        MIniReadI32(&ini, "view", &modelRadio);
        MIniFree(&ini);
    }

    if (assetsRead == AssetsRead_PC_EliteClub) {
        WriteAllModels("models-main-pc.txt" , &annotations, &modelViewer.sceneSetup);
        WriteAllModels("models-intro-pc.txt" , &annotations, &introSceneSetup);
    } else if (assetsRead == AssetsRead_Amiga_EliteClub) {
        WriteAllModels("models-main-amiga.txt" , &annotations, &modelViewer.sceneSetup);
        WriteAllModels("models-intro-amiga.txt" , &annotations, &introSceneSetup);
    } else if (assetsRead == AssetsRead_Amiga_EliteClub2) {
        WriteAllModels("models-main-amiga.txt" , &annotations, &modelViewer.sceneSetup);
        WriteAllModels("models-intro-amiga.txt" , &annotations, &introSceneSetup);
    }

    bool resetPalette = true;

    MReadFileRet mainOverridesFile{};
    MReadFileRet introOverridesFile{};
    MMemIO introMemIO{};
    MFileInfo mainOverridesFileInfo{};
    MFileInfo introOverridesFileInfo{};

    OverridesFile mainOverrides;
    OverridesFile introOverrides;
    introOverrides.override = true;
    mainOverrides.override = true;

    if (mainOverrides.override) {
        LoadModelOverridesIfChanged("data/models-main.txt", &(modelViewer.sceneSetup), &mainOverrides);
    }
    if (introOverrides.override ) {
        LoadModelOverridesIfChanged("data/model-overrides.txt", &introSceneSetup, &introOverrides);
    }

    if (modelRadio == 0) {
        RenderIntroAtTime(surfaceTexture, &surface, &intro, &introSceneSetup, &introEntity, frameOffset, &resetPalette);
    }

    i32 mouseX = 0;
    i32 mouseY = 0;
    bool modelViewerViewControl = false;

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch(event.type) {
                case SDL_QUIT:
                    done = true;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            done = true;
                            break;
                        case SDLK_F2:
                            WritePng("screenshot.png", curSceneSetup->raster->surface, sDefaultPalette);
                            break;
                    }
                    break;
                case SDL_KEYUP:
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                        done = true;
                    }
                    break;
            }

            if (modelViewerViewControl) {
                switch(event.type) {
                    case SDL_MOUSEBUTTONUP:
                        if (event.button.button == 3) {
                            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                            modelViewerViewControl = false;
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                        }
                        break;
                    case SDL_MOUSEMOTION:
                        modelViewer.yaw -= event.motion.xrel * 0x20;
                        modelViewer.pitch += event.motion.yrel * 0x20;
                        renderScene = true;
                        break;
                }
            }
        }

        u64 time = SDL_GetPerformanceCounter();
        u64 timeSinceLastCheck = ((time - lastFileCheckTime) * 100 ) / tick;
        if (timeSinceLastCheck > 100) {
            lastFileCheckTime = time;
            if (mainOverrides.override) {
                if (LoadModelOverridesIfChanged("data/models-main.txt", &(modelViewer.sceneSetup), &mainOverrides) == 0) {
                    renderScene = true;
                }
            }
            if (introOverrides.override ) {
                if (LoadModelOverridesIfChanged("data/model-overrides.txt", &introSceneSetup, &introOverrides) == 0) {
                    renderScene = true;
                }
            }
        }

        if (Audio_ModDone(&audio)) {
            Audio_ModStart(&audio, Audio_ModEnum_SILENCE);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);

        ImGui::NewFrame();
        {
            ImGui::Begin("Data2");

            if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem("Draw Tests")) {
                    if (ImGui::Button("Flares 16")) {
                        Surface_Clear(fileRasterContext.surface, 0x7f);

                        i32 tileSize = 15;
                        i32 tileOffsetX = 8;
                        i32 tileOffsetY = 8;
                        i16 x = tileOffsetX;
                        i16 y = tileOffsetY;

                        for (int j = 0; j < 320; j += (tileSize + 1)) {
                            Surface_DrawLineClipped(fileRasterContext.surface, j, 0, j, 199, 3);
                        }

                        for (int k = 0; k < 200; k += (tileSize + 1)) {
                            Surface_DrawLineClipped(fileRasterContext.surface, 0, k, 319, k, 3);
                        }

                        for (int i = 0; i < 7; i++) {
                            Surface_DrawFlare(fileRasterContext.surface, x, y, i, 1, 2);
                            x += tileSize + 1;
                        }

                        y += tileSize + 1;
                        x = tileOffsetX;
                        for (int i = 7; i < 18; i++) {
                            Surface_DrawFlare(fileRasterContext.surface, x, y, i, 1, 2);
                            x += tileSize + 1;
                            if (x > 320) {
                                x = tileOffsetX;
                                y += tileSize + 1;
                            }
                        }

                        fileRasterPalette[1].r = 0x10;
                        fileRasterPalette[1].g = 0x10;
                        fileRasterPalette[1].b = 0x70;
                        fileRasterPalette[2].r = 0x50;
                        fileRasterPalette[2].g = 0x50;
                        fileRasterPalette[2].b = 0xc0;

                        UpdateSurfaceTexture(surfaceTexture, fileRasterContext.surface, (RGB *) fileRasterPalette);
                    }

                    if (ImGui::Button("Flares 32")) {
                        Surface_Clear(fileRasterContext.surface, 0x7f);

                        i32 tileSize = 31;
                        i32 tileOffsetX = 16;
                        i32 tileOffsetY = 16;
                        i16 x = tileOffsetX;
                        i16 y = tileOffsetY;

                        for (int j = 0; j < 320; j += (tileSize + 1)) {
                            Surface_DrawLineClipped(fileRasterContext.surface, j, 0, j, 199, 3);
                        }

                        for (int k = 0; k < 200; k += (tileSize + 1)) {
                            Surface_DrawLineClipped(fileRasterContext.surface, 0, k, 319, k, 3);
                        }

                        for (int i = -9; i < 0; i++) {
                            Surface_DrawFlare(fileRasterContext.surface, x, y, i, 1, 2);
                            x += tileSize + 1;
                        }

                        y += tileSize + 1;
                        x = tileOffsetX;
                        for (int i = 0; i < 23; i++) {
                            Surface_DrawFlare(fileRasterContext.surface, x, y, i, 1, 2);
                            Surface_DrawCircleFill(fileRasterContext.surface, x, y, i - 7, 2);
                            x += tileSize + 1;
                            if (x > 320 - (tileSize + 2)) {
                                x = tileOffsetX;
                                y += tileSize + 1;
                            }
                        }

                        fileRasterPalette[1].r = 0x10;
                        fileRasterPalette[1].g = 0x10;
                        fileRasterPalette[1].b = 0x70;
                        fileRasterPalette[2].r = 0x50;
                        fileRasterPalette[2].g = 0x50;
                        fileRasterPalette[2].b = 0xc0;
                        fileRasterPalette[3].r = 0xc0;
                        fileRasterPalette[3].g = 0xc0;
                        fileRasterPalette[3].b = 0xc0;

                        UpdateSurfaceTexture(surfaceTexture, fileRasterContext.surface, (RGB *) fileRasterPalette);
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Audio")) {
                    if (ImGui::SliderInt("Volume", &volume, 0, AUDIO_VOLUME_MAX - 1)) {
                        Audio_SetVolume(&audio, volume);
                    }

                    ImGui::SliderInt("Sample", &sampleId, 5, Audio_NumSamples(&audio));

                    ImGui::SameLine();
                    if (ImGui::Button("Play Sample")) {
                        Audio_PlaySample(&audio, sampleId, sampleVolume);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Stop Sample")) {
                        Audio_CancelSample(&audio, sampleId);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Stop Samples")) {
                        Audio_StopSamples(&audio);
                    }

                    ImGui::SliderInt("SampleVolume", &sampleVolume, 0, 63);

                    ImGui::SliderInt("Engine", &engineSoundId, 1, 4);

                    ImGui::SameLine();
                    if (ImGui::Button("Play Engine")) {
                        Audio_PlayEngineSound(&audio, engineSoundId, sampleVolume, tonePeriod, 20);
                    }

                    if (ImGui::SliderInt("Period", &tonePeriod, 100, 20000)) {
                        Audio_PlayEngineSound(&audio, engineSoundId, sampleVolume, tonePeriod, 20);
                    }

                    ImGui::SliderInt("Mod", &modOffset, 1, Audio_NumMods(&audio));

                    ImGui::SameLine();
                    if (ImGui::Button("Play Mod")) {
                        Audio_ModStart(&audio, modOffset);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Stop Mod")) {
                        Audio_ModStop(&audio);
                    }

                    ImGui::Text("mod tick = %d", audio.modTick);
                    char channelMask[32];
                    MStrU32ToBinary(audio.channelMask, 4, channelMask);
                    ImGui::Text("channel mask = %s", channelMask);
                    ImGui::Text("volume = %d", audio.masterVolume);
                    ImGui::Text("volume fadestate = %d", audio.modVolumeFadeState);
                    ImGui::Text("volume fadestep = %d", audio.modVolumeFadeStep);
                    ImGui::Text("volume fadespeed = %d", audio.modVolumeFadeSpeed);

                    for (int i = 0; i < 4; i++) {
                        AudioChannelControl* modChannelData = audio.channelCtrl + i;
                        ImGui::Text("----", modChannelData->channelId);
                        ImGui::Text("channel = %d", modChannelData->channelId);
                        ImGui::Text("noteSetState = %d", modChannelData->noteSetState);
                        ImGui::Text("noteTimeRemaining = %d", modChannelData->noteTimeRemaining);
                        ImGui::Text("volume ramp = %x", modChannelData->volumeRamp);
                        ImGui::Text("volume ramp s = %x", modChannelData->volumeRampSet);
                        ImGui::Text("track = %x", modChannelData->nextTrack);
                        ImGui::Text("effect = %x", modChannelData->nextEffect);
                        ImGui::Text("data = %x", modChannelData->sampleData);
                        ImGui::Text("len = %d", modChannelData->sampleLen);
                        ImGui::Text("data x = %x", modChannelData->nextSampleData);
                        ImGui::Text("len x = %d", modChannelData->nextSampleLen);
                    }

                    int usedSamples = 0;
                    for (int i = 0; i < AUDIO_SAMPLE_CACHE_SIZE; i++) {
                        if (audio.sampleCache[i].used) {
                            usedSamples += 1;
                        }
                    }
                    ImGui::Text("cache - used samples %d", usedSamples);

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Render Stats")) {
                    char distanceBuffer1[64];
                    char distanceBuffer2[64];
                    char distanceBuffer3[64];
                    I64FormatDistance(((u64)curEntity->entityPos[0]) << curEntity->depthScale, distanceBuffer1, sizeof(distanceBuffer1));
                    I64FormatDistance(((u64)curEntity->entityPos[1]) << curEntity->depthScale, distanceBuffer2, sizeof(distanceBuffer2));
                    I64FormatDistance(((u64)curEntity->entityPos[2]) << curEntity->depthScale, distanceBuffer3, sizeof(distanceBuffer3));

                    ImGui::Text("Entity pos x: %x (%s)  y: %x (%s)  z: %x (%s)",
                                curEntity->entityPos[0], distanceBuffer1,
                                curEntity->entityPos[1], distanceBuffer2,
                                curEntity->entityPos[2], distanceBuffer3);

                    ImGui::Text("Lighting x: %hx y: %hx z: %hx",
                                curSceneSetup->lightDirView[0], curSceneSetup->lightDirView[1],
                                curSceneSetup->lightDirView[2]);

                    ImGui::Text("Vertices Projected: %d  Transformed: %d",
                                curSceneSetup->debug.projectedVertices, curSceneSetup->debug.transformedVertices);

                    ImGui::Text("Models Visited: %d  Skipped: %d",
                                curSceneSetup->debug.modelsVisited, curSceneSetup->debug.modelsSkipped);

                    ImGui::Text("Model Code Interpreted: %d", MArraySize(curSceneSetup->debug.byteCodeTrace));

                    if (curSceneSetup->debug.planetRendered) {
                        ImGui::Text("Planet: ");
                        ImGui::SameLine();
                        ImGui::Text("half: %d surface: %d horizon: %d",
                                    curSceneSetup->debug.planetHalfMode, curSceneSetup->debug.planetCloseToSurface,
                                    curSceneSetup->debug.planetHorizonScale);
                        ImGui::Text("    near: %d far: %d",
                                    curSceneSetup->debug.planetNearAxisDist, curSceneSetup->debug.planetFarAxisDist);

                        Float16FormatDistance(curSceneSetup->debug.planetOutlineDist, distanceBuffer1, sizeof(distanceBuffer1));
                        Float16FormatDistance(curSceneSetup->debug.planetAltitude, distanceBuffer2, sizeof(distanceBuffer2));
                        Float16FormatDistance(curSceneSetup->debug.planetRadius, distanceBuffer3, sizeof(distanceBuffer3));

                        ImGui::Text("    outline: %s altitude: %s radius: %s",
                                    distanceBuffer1, distanceBuffer2, distanceBuffer3);
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();

                ImGui::End();

                ImGui::Begin("Render");

                ImVec2 mousePos = ImGui::GetMousePos();
                ImVec2 imagePos = ImGui::GetCursorScreenPos();

                ImVec2 imageSize;
#if FINTRO_SCREEN_RES == 1
                imageSize.x = surface.width * 4;
                imageSize.y = surface.height * 4;
#elif FINTRO_SCREEN_RES == 2
                imageSize.x = surface.width * 2;
                imageSize.y = surface.height * 2;
#else
                imageSize.x = surface.width;
                imageSize.y = surface.height;
#endif
                ImGui::Image((void *) (intptr_t) surfaceTexture, imageSize,
                             ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 0));

                if (!modelViewerViewControl && ImGui::IsItemClicked(1)) {
                    modelViewerViewControl = true;
                    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
                }
#if FINTRO_SCREEN_RES == 1
                mouseX = (mousePos.x - imagePos.x) / 4;
                mouseY = (mousePos.y - imagePos.y) / 4;
#elif FINTRO_SCREEN_RES == 2
                mouseX = (mousePos.x - imagePos.x) / 2;
                mouseY = (mousePos.y - imagePos.y) / 2;
#else
                mouseX = (mousePos.x - imagePos.x);
                mouseY = (mousePos.y - imagePos.y);
#endif
                if (ImGui::RadioButton("Intro", &modelRadio, 0)) {
                    renderScene = true;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Main", &modelRadio, 1)) {
                    renderScene = true;
                }

                if (modelRadio == 0) {
                    if (ImGui::SliderInt("Pos", &frameOffset, 0, introNumFrames)) { // format %x doesnt work
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) {
                        frameOffset -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+")) {
                        frameOffset += 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Render")) {
                        renderScene = true;
                    }
                    scenePos = Intro_GetScenePos(&intro, frameOffset);
                    if (ImGui::SliderInt("Scene", &(scenePos.scene), 0, intro.numScenes - 1)) {
                        renderScene = true;
                        frameOffset = Intro_GetSceneFrameOffset(&intro, scenePos.scene);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("--")) {
                        if (scenePos.scene > 0) {
                            scenePos.scene -= 1;
                            frameOffset = Intro_GetSceneFrameOffset(&intro, scenePos.scene);
                            renderScene = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("++")) {
                        if (scenePos.scene < intro.numScenes - 1) {
                            scenePos.scene += 1;
                            frameOffset = Intro_GetSceneFrameOffset(&intro, scenePos.scene);
                            renderScene = true;
                        }
                    }
                    IntroScene *introScene = intro.scenes + scenePos.scene;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Play")) {
                        if (playIntro) {
                            playIntro = 0;
                            Audio_ModStop(&audio);
                        } else {
                            u64 delta = Intro_GetTimeForFrameOffset(&intro, frameOffset);
                            startTime = time - (((delta + 1) * tick) / 100);
                            playIntro = 1;
                            Audio_ModStartAt(&audio, Audio_ModEnum_FRONTIER_THEME_INTRO, (delta + 1) / 2);
                        }
                    }

                    if (ImGui::SliderInt("Frame", &(scenePos.offset), 0,
                                         introScene->frameEnd - introScene->frameStart - 1)) {
                        renderScene = true;
                        frameOffset = Intro_GetFrameOffset(&intro, &scenePos);
                    }

                    if (renderScene) {
                        RenderIntroAtTime(surfaceTexture, &surface, &intro, &introSceneSetup, &introEntity,
                                          frameOffset, &resetPalette);
                        renderScene = false;
                    }
                } else {
                    bool modelChanged = false;
                    if (ImGui::SliderInt("Model", &modelOffset, 0, numModels)) {
                        modelChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) {
                        modelOffset -= 1;
                        modelChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+")) {
                        modelOffset += 1;
                        modelChanged = true;
                    }
                    if (modelChanged) {
                        selectedModel = modelOffset;
                        ModelViewer_SetModelForScene(&modelViewer, modelOffset);
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Render")) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset")) {
                        renderScene = true;
                        ModelViewer_ResetForModel(&modelViewer);
                    }
                    if (ImGui::SliderInt("Yaw", &(modelViewer.yaw), -(1<<15), (1<<15)-1)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("yaw-")) {
                        modelViewer.yaw -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("yaw+")) {
                        modelViewer.yaw += 1;
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Pitch", &(modelViewer.pitch), -(1<<15), (1<<15)-1)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pitch-")) {
                        modelViewer.pitch -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pitch+")) {
                        modelViewer.pitch += 1;
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Roll", &(modelViewer.roll), -(1<<15), (1<<15)-1)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("roll-")) {
                        modelViewer.roll -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("roll+")) {
                        modelViewer.roll += 1;
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Pos X", &(modelViewer.pos[0]), -(1<<15), (1<<15)-1)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x--")) {
                        modelViewer.pos[0] -= 10;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pos x-")) {
                        modelViewer.pos[0] -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pos x+")) {
                        modelViewer.pos[0] += 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x++")) {
                        modelViewer.pos[0] += 10;
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Pos Y", &(modelViewer.pos[1]), -0x7fff, 0x7fff)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("y--")) {
                        modelViewer.pos[1] -= 10;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pos y-")) {
                        modelViewer.pos[1] -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pos y+")) {
                        modelViewer.pos[1] += 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("y++")) {
                        modelViewer.pos[1] += 10;
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Pos Z", &(modelViewer.pos[2]), -0x7fff, 0x7fff)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("z--")) {
                        modelViewer.pos[2] -= 10;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pos z-")) {
                        modelViewer.pos[2] -= 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("pos z+")) {
                        modelViewer.pos[2] += 1;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("z++")) {
                        modelViewer.pos[2] += 10;
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Pos Scale", &modelViewer.posScale, 0, 63)) {
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset Scale")) {
                        ModelViewer_ResetPosScale(&modelViewer);
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("View Coord Scale", &modelViewer.depthScale, -32, 32)) {
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Light Angle A", &modelViewer.lightingAngleA, -0x8000, 0x7fff)) {
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Light Angle B", &modelViewer.lightingAngleB, -0x8000, 0x7fff)) {
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Render Detail", &modelViewer.renderDetail, -2, 4)) {
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Planet Detail ", &modelViewer.planetDetail, -2, 4)) {
                        renderScene = true;
                    }
                    if (ImGui::SliderInt("Planet Min Atmos Width", &modelViewer.planetMinAtmosBandWidth, 0x4000, 0x4400)) {
                        renderScene = true;
                    }
                    if (ImGui::Button("Bad Planet1")) {
                        modelViewer.pos[0] = -198;
                        modelViewer.pos[1] = 34;
                        modelViewer.pos[2] = -413;
                        modelViewer.pitch = 417280;
                        modelViewer.yaw = -525248;
                        modelViewer.roll = 0;
                        modelViewer.modelIndex = 72;
                        renderScene = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Good Planet1")) {
                        modelViewer.pos[0] = -198;
                        modelViewer.pos[1] = 34;
                        modelViewer.pos[2] = -412;
                        modelViewer.pitch = 417280;
                        modelViewer.yaw = -525248;
                        modelViewer.roll = 0;
                        modelViewer.modelIndex = 72;
                        renderScene = true;
                    }
                    if (renderScene) {
                        RenderModelViewer(surfaceTexture, &surface, &modelViewer, modelOffset, resetPalette);
                        renderScene = false;
                    }
                }

                if (playIntro) {
                    u64 delta = ((time - startTime) * 100) / tick;

                    frameOffset = Intro_GetFrameOffsetAtTime(&intro, delta);

                    if (frameOffset < Intro_GetNumFrames(&intro)) {
                        renderScene = true;
                    } else {
                        playIntro = 0;
                        Audio_ModStop(&audio);
                    }
                }

                if (mouseX >= 0 && mouseY >= 0 && mouseX < SURFACE_WIDTH && mouseY < SURFACE_HEIGHT) {
                    i32 o = mouseY * SURFACE_WIDTH + mouseX;
                    u32 tmp = surface.insOffset[o];
                    ImGui::Text("%d,%d (%d, %d) : %05x", mouseX, mouseY, mouseX-SURFACE_WIDTH/2, mouseY-SURFACE_HEIGHT/2, tmp);
                } else {
                    ImGui::Text("%d,%d (%d, %d) : n/a", mouseX, mouseY, mouseX-SURFACE_WIDTH/2, mouseY-SURFACE_HEIGHT/2);
                }

                if (modelRadio == 0) {
                    curSceneSetup = &introSceneSetup;
                    curEntity = &introEntity;
                } else if (modelRadio == 1) {
                    curSceneSetup = &modelViewer.sceneSetup;
                    curEntity = &modelViewer.entity;
                }

                numModels = CountModels(curSceneSetup);

                ImGui::End();

                ImGui::Begin("Data");

                if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
                    if (ImGui::BeginTabItem("Scene")) {
                        i32 tickTime = curEntity->entityVars[0];
                        if (ImGui::SliderInt("Tick Time [0]", &tickTime, 0, 65535)) {
                            curEntity->entityVars[0] = tickTime;
                            renderScene = true;
                        }
                        i32 landingTime = curEntity->entityVars[1];
                        if (ImGui::SliderInt("Landing Time [1]", &landingTime, 0, 65535)) {
                            curEntity->entityVars[1] = landingTime;
                            renderScene = true;
                        }
                        i32 param2 = curEntity->entityVars[2];
                        if (ImGui::SliderInt("Param [2]", &param2, 0, 65535)) {
                            curEntity->entityVars[2] = param2;
                            renderScene = true;
                        }
                        i32 param3 = curEntity->entityVars[3];
                        if (ImGui::SliderInt("Param [3]", &param3, 0, 65535)) {
                            curEntity->entityVars[3] = param3;
                            renderScene = true;
                        }
                        i32 param4 = curEntity->entityVars[4];
                        if (ImGui::SliderInt("Param [4]", &param4, 0, 65535)) {
                            curEntity->entityVars[4] = param4;
                            renderScene = true;
                        }
                        i32 param5 = curEntity->entityVars[5];
                        if (ImGui::SliderInt("Param [5]", &param5, 0, 65535)) {
                            curEntity->entityVars[5] = param5;
                            renderScene = true;
                        }
                        i32 time1 = curEntity->entityVars[7];
                        if (ImGui::SliderInt("Time Of Day [6]", &time1, 0, 65535)) {
                            curEntity->entityVars[7] = time1;
                            renderScene = true;
                        }
                        i32 time2 = curEntity->entityVars[6];
                        if (ImGui::SliderInt("Time Of Day [7]", &time2, 0, 65535)) {
                            curEntity->entityVars[6] = time2;
                            renderScene = true;
                        }
                        i32 date1 = curEntity->entityVars[9];
                        if (ImGui::SliderInt("Date [8]", &date1, 0, 65535)) {
                            curEntity->entityVars[9] = date1;
                            renderScene = true;
                        }
                        i32 date2 = curEntity->entityVars[8];
                        if (ImGui::SliderInt("Date [9]", &date2, 0, 65535)) {
                            curEntity->entityVars[8] = date2;
                            renderScene = true;
                        }
                        i32 leftTrust = curEntity->entityVars[13];
                        if (ImGui::SliderInt("Left/Right Thrust [13]", &leftTrust, 0, 65535)) {
                            curEntity->entityVars[13] = leftTrust;
                            renderScene = true;
                        }
                        i32 rightThrust = curEntity->entityVars[14];
                        if (ImGui::SliderInt("Up/Down Thrust [14]", &rightThrust, 0, 65535)) {
                            curEntity->entityVars[14] = rightThrust;
                            renderScene = true;
                        }
                        i32 mainTrust = curEntity->entityVars[15];
                        if (ImGui::SliderInt("Forward/Back Thrust [15]", &mainTrust, 0, 65535)) {
                            curEntity->entityVars[15] = mainTrust;
                            renderScene = true;
                        }
                        i32 missile1 = curEntity->entityVars[29];
                        if (ImGui::SliderInt("Missile [29]", &missile1, 0, 65535)) {
                            curEntity->entityVars[29] = missile1;
                            renderScene = true;
                        }
                        i32 missile2 = curEntity->entityVars[30];
                        if (ImGui::SliderInt("Missile [30]", &missile2, 0, 65535)) {
                            curEntity->entityVars[30] = missile2;
                            renderScene = true;
                        }
                        i32 missile3 = curEntity->entityVars[31];
                        if (ImGui::SliderInt("Missile [31]", &missile3, 0, 65535)) {
                            curEntity->entityVars[31] = missile3;
                            renderScene = true;
                        }
                        i32 missile4 = curEntity->entityVars[32];
                        if (ImGui::SliderInt("Missile [32]", &missile4, 0, 65535)) {
                            curEntity->entityVars[32] = missile4;
                            renderScene = true;
                        }
                        i32 missile5 = curEntity->entityVars[33];
                        if (ImGui::SliderInt("Missile [33]", &missile5, 0, 65535)) {
                            curEntity->entityVars[33] = missile5;
                            renderScene = true;
                        }

                        if (ImGui::InputText("Entity Text", curEntity->entityText, 20)) {
                            renderScene = true;
                        }

                        ImGui::RadioButton("Entity Ours", &showEntityHexView, 0);
                        ImGui::SameLine();
                        if (modelRadio == 0) {
                            ImGui::RadioButton("Entity Exe", &showEntityHexView, 1);
                            ImGui::SameLine();
                        }
                        ImGui::RadioButton("Entity Vars", &showEntityHexView, 2);

                        if (showEntityHexView == 0) {
                            hexEditor.DrawContents(curEntity, sizeof(*curEntity),
                                                   curSceneSetup->debug.renderDataOffset);
                        } else if (showEntityHexView == 1 && modelRadio == 0) {
                            hexEditor.DrawContents(curSceneSetup->debug.renderData, 0x150,
                                                   curSceneSetup->debug.renderDataOffset);
                        } else if (showEntityHexView == 2) {
                            hexEditor.DrawContents(curEntity->entityVars, sizeof(curEntity->entityVars),
                                                   curSceneSetup->debug.renderDataOffset + 72);
                        }

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Model")) {
                        if (sModelRendered) {
                            ImGui::RadioButton("Text", &modelTab, 0);
                            ImGui::SameLine();
                            ImGui::RadioButton("Hex", &modelTab, 1);
                            ImGui::SameLine();
                            bool copy = ImGui::Button("Copy");
                            ImGui::SameLine();
                            bool hide = curSceneSetup->debug.hideModel[selectedModel];
                            if (ImGui::Checkbox("Hide", &hide)) {
                                curSceneSetup->debug.hideModel[selectedModel] = hide;
                                renderScene = true;
                            }
                            ImGui::SameLine();
                            bool hexCodeComment = debugModelParams.hexCodeComment;
                            if (ImGui::Checkbox("Hex Comment", &hexCodeComment)) {
                                debugModelParams.hexCodeComment = hexCodeComment;
                            }
                            ImGui::SameLine();
                            bool codeOffsets = debugModelParams.codeOffsets;
                            if (ImGui::Checkbox("Code Offsets", &codeOffsets)) {
                                debugModelParams.codeOffsets = codeOffsets;
                            }

                            {
                                ModelData* model = Render_GetModel(curSceneSetup, curEntity->modelIndex);
                                if (model) {
                                    u16 modelSizeScale = model->scale1 + model->scale2;
                                    u64 modelSize = ((u64)model->radius) << modelSizeScale;

                                    char objectSizeBuffer[64];
                                    I64FormatDistance(modelSize, objectSizeBuffer, sizeof(objectSizeBuffer));
                                    ImGui::Text("Bounding radius: %s", objectSizeBuffer);
                                }
                            }

                            ImGui::InputText("##comment", textInputBuff, textInputBuffSize);
                            ImGui::SameLine();
                            bool addComment = ImGui::Button("Add");
                            if (addComment) {
                                if (annotations.addRaw(textInputBuff)) {
                                    annotations.save(annotationsFile);
                                }
                            }

                            ImGui::BeginChild("ModelIndexes", ImVec2(108, -1), false,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            ImGui::PushItemWidth(95);
                            MStrArray strArray(10);
                            MStrWriter tmpWriter;
                            tmpWriter.init(20);
                            tmpWriter.clear();
                            int foundNull = 0;
                            int i = 0;
                            uint32_t rootIndex = curEntity->modelIndex;
                            u32Array *loadedModelIndexes = &curSceneSetup->debug.loadedModelIndexes;
                            while (true) {
                                u32 modelOffset2 = Render_GetModelCodeOffset(curSceneSetup, i);
                                if (modelOffset2 == 0) {
                                    foundNull++;
                                    if (foundNull > 1 && i > 2) {
                                        break;
                                    }
                                } else {
                                    foundNull = 0;
                                }
                                if (i == rootIndex) {
                                    tmpWriter.appendf("%03d: %05x <", i, modelOffset2);
                                } else {
                                    bool found = false;
                                    for (int j = 0; j < MArraySize(*loadedModelIndexes); ++j) {
                                        if (loadedModelIndexes->arr[j] == i) {
                                            found = true;
                                        }
                                    }
                                    if (found) {
                                        tmpWriter.appendf("%03d: %05x *", i, modelOffset2);
                                    } else {
                                        tmpWriter.appendf("%03d: %05x", i, modelOffset2);
                                    }
                                }
                                strArray.addCopy(tmpWriter.data());
                                tmpWriter.clear();
                                i++;
                            }

                            ImGui::ListBox("", &selectedModel, strArray.data(),
                                           strArray.size(), strArray.size());
                            ImGui::EndChild();
                            ImGui::SameLine();
                            ModelData *modelData = Render_GetModel(curSceneSetup, selectedModel);
                            u64 byteCodeBegin = 0;
                            MMemIO modelDecompile;
                            MMemInitAlloc(&modelDecompile, 100);

                            if (modelData != NULL) {
                                byteCodeBegin = ((u8 *) modelData - curSceneSetup->debug.modelDataFileStartAddress) +
                                                modelData->codeOffset;

                                DebugModelInfo modelInfo;
                                debugModelParams.maxSize = 0xfff;
                                debugModelParams.offsetBegin = byteCodeBegin;
                                if (copy) {
                                    debugModelParams.byteCodeTrace = NULL;
                                } else {
                                    debugModelParams.byteCodeTrace = &curSceneSetup->debug.byteCodeTrace;
                                }

                                DecompileModel(modelData, selectedModel, &debugModelParams, &modelInfo, &modelDecompile);
                                MArrayFree(modelInfo.modelIndexes);
                            }

                            if (modelTab == 0) {
                                if (copy) {
                                    ImGui::LogToClipboard();
                                }
                                ImGui::BeginChild("ByteCodeTextArea", ImVec2(0, 0), false,
                                                  ImGuiWindowFlags_HorizontalScrollbar);
                                ImGui::TextUnformatted((char *) modelDecompile.mem, (char *) modelDecompile.pos);
                                ImGui::EndChild();
                                if (copy) {
                                    ImGui::LogFinish();
                                }
                            } else {
                                if (modelData != NULL) {
                                    u32 modelBegin = ((u8*) modelData - curSceneSetup->debug.modelDataFileStartAddress);
                                    hexEditor.DrawContents(modelData, 0xff, modelBegin);
                                }
                            }

                            MMemFree(&modelDecompile);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Draw")) {
                        ImGui::RadioButton("Text", &drawTab, 0);
                        ImGui::SameLine();
                        ImGui::RadioButton("Hex", &drawTab, 1);
                        ImGui::SameLine();
                        bool copy = ImGui::Button("Copy");

                        if (drawTab == 0) {
                            if (copy) {
                                ImGui::LogToClipboard();
                            }
                            DebugDrawInfo drawInfo;
                            MStrWriter writer;
                            writer.init(100);
                            DumpDrawInfo(&raster, drawInfo, &writer);
                            ImGui::BeginChild("DrawTextArea", ImVec2(0, 0), false,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            ImGui::TextUnformatted(writer.data(), writer.data() + writer.size());
                            ImGui::EndChild();
                            if (copy) {
                                ImGui::LogFinish();
                            }
                        } else {
                            hexEditor.DrawContents(raster.depthTree.data, raster.depthTree.size, 0x000);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Palette")) {
                        bool copy = ImGui::Button("Copy");
                        if (copy) {
                            ImGui::LogToClipboard();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Reset")) {
                            resetPalette = true;
                            renderScene = true;
                        }
                        DebugPaletteInfo paletteInfo;
                        paletteInfo.writer.init(100);
                        DumpPaletteInfo(&raster, sDefaultPalette, paletteInfo);
                        ImGui::BeginChild("PaletteTextArea", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
                        ImGui::TextUnformatted(paletteInfo.writer.data(),
                                               paletteInfo.writer.data() + paletteInfo.writer.size());
                        ImGui::EndChild();
                        if (copy) {
                            ImGui::LogFinish();
                        }
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
                ImGui::End();
            }
        }

        ImGui::Render();
        SDL_GL_MakeCurrent(window, glContext);
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    MIniSave iniSave;
    if (MIniSaveFile(&iniSave, "persist.ini") == 0) {
        MIniMWriteI32(&iniSave, "frameOffset", frameOffset);
        MIniMWriteI32(&iniSave, "modelOffset", modelOffset);
        MIniMWriteI32(&iniSave, "modelViewer.pitch", modelViewer.pitch);
        MIniMWriteI32(&iniSave, "modelViewer.yaw", modelViewer.yaw);
        MIniMWriteI32(&iniSave, "modelViewer.roll", modelViewer.roll);
        MIniMWriteI32(&iniSave, "modelViewer.x", modelViewer.pos[0]);
        MIniMWriteI32(&iniSave, "modelViewer.y", modelViewer.pos[1]);
        MIniMWriteI32(&iniSave, "modelViewer.z", modelViewer.pos[2]);
        MIniMWriteI32(&iniSave, "modelViewer.lightingAngleA", modelViewer.lightingAngleA);
        MIniMWriteI32(&iniSave, "modelViewer.lightingAngleB", modelViewer.lightingAngleB);
        MIniMWriteI32(&iniSave, "view", modelRadio);
        MIniSaveFree(&iniSave);
    }

    Audio_Exit(&audio);
    Intro_Free(&intro, &introSceneSetup);
    ModelViewer_Free(&modelViewer);
    Raster_Free(&raster);
    Raster_Free(&fileRasterContext);
    Surface_Free(&surface);
    Render_Free(&introSceneSetup);
    Assets_FreeAmigaFiles(&assetsDataAmiga);
    FreeModelOverridesFile(&introOverrides);
    FreeModelOverridesFile(&mainOverrides);

    Render_Free(&modelViewer.sceneSetup);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    MMemDebugDeinit();

    return 0;
}
