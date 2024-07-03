#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <devices/timer.h>
#include <cybergraphx/cybergraphics.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>

#ifdef __GNUC__
#include <inline/cybergraphics.h>
#else
#include <proto/cybergraphics.h>
#endif

#include "assets.h"
#include "audio.h"
#include "fintro.h"
#include "mlib.h"
#include "render.h"
#include "sourcediffs.h"

#define KC_ESC 0x45
#define KC_TILDE 0x00
#define KC_SPACE 0x40
#define KC_F1 0x50

// Audio runs at 50 frames per second
#define AUDIO_TICK_DELAY_MICROS (20 * 1000)

// A lot of stack is used by the renderer
#define MIN_STACK_BYTES 100000

#ifdef __GNUC__
#define MNOREMOVE __attribute__((used))
#else
#define MNOREMOVE
#endif

#ifndef BUILD_ID
#define BUILD_ID "Unknown build : #define BUILD_ID"
#endif

const STRPTR MNOREMOVE VERSTRING = "$VER: Frontier: Elite2 Intro (" MMACRO_QUOTE(BUILD_ID) ") Mark Watson";
const STRPTR MNOREMOVE STACKSIZE = "$STACK:" MMACRO_QUOTE(MIN_STACK_BYTES) "\n";

#ifdef __VBCC__
size_t __stack = MIN_STACK_BYTES;
#endif

#ifndef BUILD_INFO
#define BUILD_INFO "Unknown build : #define BUILD_INFO"
#endif

const char* BUILD_STRING = MMACRO_QUOTE(BUILD_INFO);

struct IntuitionBase* IntuitionBase;
struct GfxBase* GfxBase;
#ifdef __GNUC__
static
#endif
struct Library* CyberGfxBase;
struct IORequest TimerDevice;
struct Device* TimerBase; // extern'd/exported so AOS timer func stubs can use

static struct Screen* aosScreen;
static struct Window* aosWindow;

static int sScreenWidth = 0;
static int sScreenHeight = 0;
static int sRender = TRUE;
static int sWaitTOF = TRUE;
static int sFrameOffset = 0;
static int sDebugMode = 0;
static int sPause = 0;

static u32 sRasterTime;
static u32 sRenderTime;
static u32 sTotalTime;
static u32 sCopyTime;
static u32 sPalTime;

static ULONG sClockTickInterval;

static u16 sFIntroPalette[256];

static Intro sIntro;
static AudioContext sAudio;
static u64 sStartTime;
static u64 sCurrentClock;

static u16 sModToPlay = Audio_ModEnum_FRONTIER_THEME_INTRO;
static const char* sFrontierExePath;
static b32 sProfile;
static b32 sProfileFrame;
static b32 sProfileSteps;

#define INTRO_OVERRIDES "data/model-overrides-be.dat"

static UWORD sMouseCursorNullGraphic[] = {
        0x0000, 0x0000, // reserved, must be NULL
        0x0000, 0x0000, // 1 row of image data
        0x0000, 0x0000  // reserved, must be NULL
};

static u16 sInitialPaletteColours[2] = {
        0x0000,
        0x0fff,
};

enum AudioCallbackStateEnum {
    AUDIO_STATE_OFF = 0,
    AUDIO_STATE_ON = 1,
    AUDIO_STATE_STOPPED = 2
};

struct AudioTimerCallbackInfo {
    enum AudioCallbackStateEnum state;
    AudioContext* audioContext;
    struct MsgPort timerPort;
    struct Interrupt softInterrupt;
    struct timerequest* ioRequest;
};

static struct AudioTimerCallbackInfo sAudioTimer;

u64 AOS_GetClockCount(void) {
    struct EClockVal clock;
    ReadEClock(&clock);
    return (((u64) clock.ev_hi) << 32u) | clock.ev_lo;
}

u64 AOS_GetClockCountAndInterval(ULONG* tickInterval) {
    struct EClockVal clock;
    *tickInterval = ReadEClock(&clock);
    return (((u64) clock.ev_hi) << 32u) | clock.ev_lo;
}

void AOS_cleanupAndExit(int exitCode) {
    if (aosWindow) {
        ClearPointer(aosWindow);
        CloseWindow(aosWindow);
        aosWindow = 0;
    }

    if (aosScreen) {
        CloseScreen(aosScreen);
        aosScreen = 0;
    }

    if (CyberGfxBase) {
        CloseLibrary(CyberGfxBase);
    }

    if (GfxBase) {
        CloseLibrary((struct Library*) GfxBase);
    }

    if (IntuitionBase) {
        CloseLibrary((struct Library*) IntuitionBase);
    }

    if (TimerDevice.io_Device) {
        CloseDevice(&TimerDevice);
    }

    exit(exitCode);
}

void AOS_init(void) {
    if (!(IntuitionBase = (struct IntuitionBase*) OpenLibrary((UBYTE*) "intuition.library", 39))) {
        MLog("Unable to open intuition...");
        AOS_cleanupAndExit(0);
    }

    struct Process* thisProcess = (struct Process*) FindTask(NULL);
    ULONG stackSize = (u8*)thisProcess->pr_Task.tc_SPUpper - (u8*)thisProcess->pr_Task.tc_SPLower;
    if (stackSize < MIN_STACK_BYTES) {
        MLogf("Stack size (%d bytes) too small - must be at least %d bytes.", stackSize, MIN_STACK_BYTES);
        AOS_cleanupAndExit(0);
    }

    OpenDevice((CONST_STRPTR)"timer.device", 0, &TimerDevice, 0);
    TimerBase = TimerDevice.io_Device;
}

void AOS_screen_init(void) {
    if (!(GfxBase = (struct GfxBase*) OpenLibrary((UBYTE*) "graphics.library", 0))) {
        AOS_cleanupAndExit(0);
    }

    if (!(CyberGfxBase = OpenLibrary(CYBERGFXNAME, 41))) {
        AOS_cleanupAndExit(0);
    }

    ULONG modeId = INVALID_ID;

    struct List* list = AllocCModeListTags(CYBRMREQ_MinDepth, 8,
                              CYBRMREQ_MaxDepth, 8,
                              TAG_END);

    if (list) {
        struct Node* node = list->lh_Head;
        struct CyberModeNode* bestCgxNode = NULL;
        ULONG bestWidthDiff = 0;
        ULONG bestHeightDiff = 0;
        for (; node->ln_Succ != NULL; node = node->ln_Succ) {
            struct CyberModeNode* cgxNode = (struct CyberModeNode*) node;
            if (cgxNode->Depth == 8 && cgxNode->Width >= SURFACE_WIDTH && cgxNode->Height >= SURFACE_HEIGHT) {
                if (bestCgxNode != NULL) {
                    ULONG widthDiff = cgxNode->Width - SURFACE_WIDTH;
                    ULONG heightDiff = cgxNode->Height - SURFACE_HEIGHT;
                    if (widthDiff > bestWidthDiff) {
                        continue;
                    }
                    if (heightDiff > bestHeightDiff) {
                        continue;
                    }
                }
                bestWidthDiff = cgxNode->Width - SURFACE_WIDTH;
                bestHeightDiff = cgxNode->Height - SURFACE_HEIGHT;
                bestCgxNode = cgxNode;
            }
        }
        if (bestCgxNode) {
            modeId = bestCgxNode->DisplayID;
//            MLogf("Using screen mode: %dx%d@%d", bestCgxNode->Width, bestCgxNode->Height, bestCgxNode->Depth);
        }
        FreeCModeList(list);
    }

    if (modeId == INVALID_ID) {
        AOS_cleanupAndExit(0);
    }

    sScreenWidth = GetCyberIDAttr(CYBRIDATTR_WIDTH, modeId);
    sScreenHeight = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeId);

    aosScreen = OpenScreenTags(NULL,
                               SA_Depth, 8,
                               SA_DisplayID, modeId,
                               SA_Width, sScreenWidth,
                               SA_Height, sScreenHeight,
                               SA_Type, CUSTOMSCREEN,
                               SA_Quiet, TRUE,
                               SA_ShowTitle, FALSE,
                               SA_Draggable, FALSE,
                               SA_Exclusive, TRUE,
                               SA_AutoScroll, FALSE,
                               TAG_END);

    if (aosScreen == NULL) {
        AOS_cleanupAndExit(0);
    }

    LoadRGB4(&aosScreen->ViewPort, sInitialPaletteColours, 2);

    aosWindow = OpenWindowTags(NULL,
                               WA_Left, 0,
                               WA_Top, 0,
                               WA_Width, sScreenWidth,
                               WA_Height, sScreenHeight,
                               WA_CustomScreen, aosScreen,
                               WA_Title, NULL,
                               WA_Backdrop, TRUE,
                               WA_Borderless, TRUE,
                               WA_DragBar, FALSE,
                               WA_Activate, TRUE,
                               WA_SmartRefresh, TRUE,
                               WA_NoCareRefresh, TRUE,
                               WA_Activate, TRUE,
                               WA_RMBTrap, TRUE,
                               WA_ReportMouse, TRUE,
                               WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_ACTIVEWINDOW,
                               TAG_DONE);

    if (!aosWindow) {
        AOS_cleanupAndExit(0);
    }

    // Empty pointer
    SetPointer(aosWindow, sMouseCursorNullGraphic, 1, 16, 0, 0);
}

static int AOS_processEvents(void) {
    const int speed1 = 10;
    const int speed2 = 100;

    struct IntuiMessage* msg;
    int close = FALSE;

    while ((msg = (struct IntuiMessage*) GetMsg(aosWindow->UserPort))) {
        switch (msg->Class) {
            case IDCMP_CLOSEWINDOW:
                close = TRUE;
                break;
            case IDCMP_RAWKEY: {
                WORD code = msg->Code & ~IECODE_UP_PREFIX;
                if (code == KC_ESC) {
                    close = TRUE;
                } else if (!(msg->Code & IECODE_UP_PREFIX)) {
                    if (code == CURSORLEFT) {
                        sRender = TRUE;
                        if (msg->Qualifier & IEQUALIFIER_LSHIFT) {
                            sFrameOffset -= speed1;
                        } else {
                            sFrameOffset -= 1;
                        }
                    } else if (code == CURSORRIGHT) {
                        sRender = TRUE;
                        if (msg->Qualifier & IEQUALIFIER_LSHIFT) {
                            sFrameOffset += speed1;
                        } else {
                            sFrameOffset += 1;
                        }
                    } else if (code == CURSORDOWN) {
                        sRender = TRUE;
                        if (msg->Qualifier & IEQUALIFIER_LSHIFT) {
                            sFrameOffset -= speed2;
                        } else {
                            sFrameOffset -= speed1;
                        }
                    } else if (code == CURSORUP) {
                        sRender = TRUE;
                        if (msg->Qualifier & IEQUALIFIER_LSHIFT) {
                            sFrameOffset += speed2;
                        } else {
                            sFrameOffset += speed1;
                        }
                    } else if (code == KC_TILDE) {
                        sDebugMode = !sDebugMode;
                    } else if (code == KC_SPACE) {
                        sPause = !sPause;
                        if (sModToPlay) {
                            if (sPause) {
                                Audio_ModStop(&sAudioContext);
                            } else {
                                u64 delta = Intro_GetTimeForFrameOffset(&sIntro, sFrameOffset);
                                sStartTime = sCurrentClock - (((delta + 1) * sClockTickInterval) / 100);
                                Audio_ModStartAt(&sAudioContext, sModToPlay, (delta + 1) / 2);
                            }
                        }
                    } else if (code == KC_F1) {
                        sWaitTOF = !sWaitTOF;
                    }
                }
                break;
            }
            case IDCMP_MOUSEBUTTONS: {
                WORD code = msg->Code;
                if (code == SELECTDOWN) {
                    close = TRUE;
                }
                break;
            }
        }
        ReplyMsg((struct Message*) msg);
    }

    return !close;
}

static void Palette_CopyDynamicColours(PaletteContext* context) {
    UpdateColour* updateColours = context->updateColours;
    u32 numUpdateColours = context->updatedColoursNum;

    if (aosScreen) {
        for (int i = 0; i < numUpdateColours; ++i) {
            UpdateColour* colour = updateColours + i;
            SetRGB4(&aosScreen->ViewPort, colour->index,
                    (colour->colour & 0xf00) >> 8,
                    (colour->colour & 0x0f0) >> 4,
                    (colour->colour & 0x00f));
        }
    }
}

static void RenderIntroAtTime(Intro* intro, SceneSetup* sceneSetup, int frameOffset, b32 resetPalette, u64 startClock) {
    Intro_SetSceneForFrameOffset(intro, sceneSetup, frameOffset);

    Palette_SetupForNewFrame(&sceneSetup->raster->paletteContext, resetPalette);
    Render_RenderScene(sceneSetup);
    u64 renderClock = AOS_GetClockCount();
    sRenderTime = ((renderClock - startClock) * 10000) / sClockTickInterval;

    Palette_CalcDynamicColourUpdates(&sceneSetup->raster->paletteContext);
    Palette_CopyDynamicColours(&sceneSetup->raster->paletteContext);

    u64 loadPalClock = AOS_GetClockCount();
    sPalTime = ((loadPalClock - renderClock) * 10000) / sClockTickInterval;

    Surface_Clear(sceneSetup->raster->surface, 0x7f);
    Raster_Draw(sceneSetup->raster);
    u64 rasterClock = AOS_GetClockCount();

    Intro_Post3dRender(intro, sceneSetup, frameOffset);

    sRasterTime = ((rasterClock - renderClock) * 10000) / sClockTickInterval;
}

#if defined(__GNUC__)
void AudioProgressTick(register struct AudioTimerCallbackInfo* audioTimer __asm("a1")) {
#else
void AudioProgressTick(__reg("a1") struct AudioTimerCallbackInfo* audioTimer) {
#endif
    struct timerequest* ioRequest = (struct timerequest *)GetMsg(&(audioTimer->timerPort));

    if (ioRequest && (audioTimer->state == AUDIO_STATE_ON)) {
        Audio_ProgressTick(audioTimer->audioContext);
        ioRequest->tr_node.io_Command = TR_ADDREQUEST;
        ioRequest->tr_time.tv_micro = AUDIO_TICK_DELAY_MICROS;
        BeginIO((struct IORequest *)ioRequest);
    } else {
        Audio_ModStart(audioTimer->audioContext, Audio_ModEnum_SILENCE);
        Audio_ProgressTick(audioTimer->audioContext);

        audioTimer->state = AUDIO_STATE_STOPPED;
    }
}

void StartAudioTickTimer(AudioContext* audioContext) {
    sAudioTimer.audioContext = audioContext;

    /* Initialize message list */
    /* Allocate message port, data & interrupt structures. Don't use CreatePort() */
    /* or CreateMsgPort() since they allocate a signal (don't need that) for a    */
    /* PA_SIGNAL type port. We need PA_SOFTINT.                                   */
    NewList(&(sAudioTimer.timerPort.mp_MsgList));

    /* Set up the (software)interrupt structure. Note that this task runs at  */
    /* priority 0. Software interrupts may only be priority -32, -16, 0, +16, */
    /* +32. Also, note that the correct node type for a software interrupt is   */
    /* NT_INTERRUPT. (NT_SOFTINT is an internal Exec flag). */
    sAudioTimer.softInterrupt.is_Code = AudioProgressTick;
    sAudioTimer.softInterrupt.is_Data = &sAudioTimer;
    sAudioTimer.softInterrupt.is_Node.ln_Pri = 0;

    sAudioTimer.timerPort.mp_Node.ln_Type = NT_MSGPORT;
    sAudioTimer.timerPort.mp_Flags = PA_SOFTINT;
    sAudioTimer.timerPort.mp_SigTask = (struct Task *)&sAudioTimer.softInterrupt;

    sAudioTimer.ioRequest = (struct timerequest *) CreateIORequest(&sAudioTimer.timerPort, sizeof(struct timerequest));
    if (!sAudioTimer.ioRequest) {
        MLog("CreateIORequest failed");
        return;
    }

    if ((OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)sAudioTimer.ioRequest, 0))) {
        MLog("OpenDevice failed");
        return;
    }

    sAudioTimer.state = AUDIO_STATE_ON;

    sAudioTimer.ioRequest->tr_node.io_Command = TR_ADDREQUEST;
    sAudioTimer.ioRequest->tr_time.tv_micro = AUDIO_TICK_DELAY_MICROS;

    BeginIO((struct IORequest *)sAudioTimer.ioRequest);
}

void StopAudioTickTimer(void) {
    // Wait for audio timer to stop
    sAudioTimer.state = AUDIO_STATE_OFF;
    int  i = 0;
    while (sAudioTimer.state != AUDIO_STATE_STOPPED && i < 100) {
        i++;
        Delay(10);
    }

    if (sAudioTimer.ioRequest) {
        CloseDevice((struct IORequest *)sAudioTimer.ioRequest);

        DeleteIORequest(&sAudioTimer.ioRequest->tr_node);
    }
}

static b32 PathAppend(char* dirInOut, u32 dirLen, const char* fileName) {
    if (dirLen < 1) {
        return FALSE;
    }

    char lastChar = dirInOut[dirLen - 1];

    u32 fileNameLen = MStrLen(fileName);

    if (lastChar != ':') {
        dirInOut[dirLen++] = '/';
    }

    memcpy(dirInOut + dirLen, fileName, fileNameLen + 1);

    return TRUE;
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
            } else if (MStrCmp("pf", arg + 1) == 0) {
                sProfileFrame = TRUE;
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
            } else if (MStrCmp("p", arg + 1) == 0) {
                sProfile = TRUE;
            } else if (MStrCmp("ps", arg + 1) == 0) {
                sProfile = TRUE;
                sProfileSteps = TRUE;
            }
        } else {
            sFrontierExePath = arg;
        }
    }

    return 0;
}

void* AmigaMalloc(void* alloc, size_t size) {
    void* mem = AllocMem(size, MEMF_ANY);
    return mem;
}

void* AmigaRealloc(void* alloc, void* mem, size_t oldSize, size_t newSize) {
    if (newSize > 0) {
        void* newMem = AllocMem(newSize, MEMF_ANY);
        if (oldSize) {
            memcpy(newMem, mem, oldSize);
            FreeMem(mem, oldSize);
            return newMem;
        }
    } else {
        FreeMem(mem, oldSize);
        return 0;
    }
}

void AmigaFree(void* alloc, void* mem, size_t size) {
    FreeMem(mem, size);
}

static MAllocator sAmigaAlloc = {
    AmigaMalloc,
    AmigaRealloc,
    AmigaFree
};

__stdargs int main(int argc, char** argv) {
    MMemAllocSet(&sAmigaAlloc);

    AOS_init();

    MReadFileRet amigaExeData;

    ParseCommandLine(argc, argv);

    if (sFrontierExePath) {
        amigaExeData = MFileReadFully(sFrontierExePath);
        if (!amigaExeData.size) {
            MLogf("Unable to load Frontier exe '%s'", sFrontierExePath);
            AOS_cleanupAndExit(0);
        }
    } else {
        // Try to load files from current directory
        amigaExeData = MFileReadFully("game");
        if (!amigaExeData.size) {
            amigaExeData = MFileReadFully("frontier");
        }
        if (!amigaExeData.size) {
            // Try to load files from program directory
            LONG lock = GetProgramDir();
            u8 directory[1024];
            if (NameFromLock(lock, directory, sizeof(directory) - 20)) {
                int pathEnd = -1;
                char lastChar = 0;
                for (int i = 0; i < sizeof(directory); ++i) {
                    if (directory[i] == 0) {
                        pathEnd = i;
                        break;
                    }
                    lastChar = directory[i];
                }

                if (pathEnd > 0) {
                    const char* gameName1 = "/game";
                    if (lastChar == ':') {
                        memcpy(directory + pathEnd, gameName1, MStrLen(gameName1) + 1);
                        MLogf("%s", directory);
                    }

                    PathAppend(directory, pathEnd, "game");
                    amigaExeData = MFileReadFully(directory);
                    if (!amigaExeData.size) {
                        PathAppend(directory, pathEnd, "frontier");
                        amigaExeData = MFileReadFully(directory);
                    }
                }
            }

            if (!amigaExeData.size) {
                MLog("Unable to find Frontier exe 'game' or 'frontier'");
                AOS_cleanupAndExit(0);
            }
        }
    }

    if (sProfileFrame) {
        MLog(BUILD_STRING);
    }

    int fpsFrames = 0;
    int totalFrames = 0;
    int fps = 0;
    ULONG updateFpsTimer = 0;
    char frameRateString[128];

    u64 prevClock = AOS_GetClockCountAndInterval(&sClockTickInterval);
    sStartTime = prevClock;

    AssetsReadEnum assetsRead = AssetsRead_Amiga_EliteClub;

    Surface surface;
    surface.pixels = 0;
    Surface_Init(&surface, SURFACE_WIDTH, SURFACE_HEIGHT);

    FMath_BuildLookupTables();

    RasterContext raster;
    raster.surface = &surface;
    raster.legacy = 0;
    Raster_Init(&raster);

    SceneSetup introSceneSetup;
    Render_Init(&introSceneSetup, &raster);

    AssetsDataAmiga assetsDataAmiga;

    // Load intro file data
    Assets_LoadAmigaFiles(&assetsDataAmiga, &amigaExeData, assetsRead);

    if (sModToPlay) {
        Audio_Init(&sAudioContext, amigaExeData.data, amigaExeData.size);
        Audio_ModStart(&sAudioContext, sModToPlay);
    }

    Intro_InitAmiga(&sIntro, &introSceneSetup, &assetsDataAmiga);

    // Read model overrides if exists
    ModelsArray overrideModels;
    MArrayInit(overrideModels);
    MReadFileRet overridesFile = Assets_LoadModelOverrides(INTRO_OVERRIDES, &overrideModels);
    for (int i = 0; i < MArraySize(overrideModels); i++) {
        if (overrideModels.arr[i]) {
            MArraySet(introSceneSetup.assets.models, i, overrideModels.arr[i]);
        }
    }

    u32 numFrames = Intro_GetNumFrames(&sIntro);

    Palette_SetupForNewFrame(&raster.paletteContext, TRUE);
    Palette_CopyFixedColours16(&raster.paletteContext, sFIntroPalette);

    MMemIO stats;
    if (sProfile) {
        MMemInitAlloc(&stats, 1000);
    }

    if (sProfileFrame) {
#define SAMPLES 100
#define WARMUP_SAMPLES 10
        MLogf("Profiling frame %d [samples x%d]...", sFrameOffset, SAMPLES);
        for (int i = 0; i < WARMUP_SAMPLES; i++) {
            sCurrentClock = AOS_GetClockCount();
            RenderIntroAtTime(&sIntro, &introSceneSetup, sFrameOffset, FALSE, sCurrentClock);
        }
        u32 rasterTimes[SAMPLES];
        u32 renderTimes[SAMPLES];
        for (int i = 0; i < SAMPLES; i++) {
            sCurrentClock = AOS_GetClockCount();
            RenderIntroAtTime(&sIntro, &introSceneSetup, sFrameOffset, FALSE, sCurrentClock);
            rasterTimes[i] = sRasterTime;
            renderTimes[i] = sRenderTime;
        }

        u32 avgRender = 0;
        u32 avgRaster = 0;
        for (int i = 0; i < SAMPLES; i++) {
            avgRender += renderTimes[i];
            avgRaster += rasterTimes[i];
        }

        avgRender /= SAMPLES;
        avgRaster /= SAMPLES;

        MLogf("render: %d raster: %d", avgRender, avgRaster);
    } else if (sProfileSteps) {
#define SAMPLES_REPEAT 100
#define WARMUP_SAMPLES 10
#define FRAME_STEP 500
        MLogf("Profiling frames [samples x%d]...", SAMPLES_REPEAT);
        for (int i = 0; i < WARMUP_SAMPLES; i++) {
            sCurrentClock = AOS_GetClockCount();
            RenderIntroAtTime(&sIntro, &introSceneSetup, sFrameOffset, FALSE, sCurrentClock);
        }

        MMemWriteU16BE(&stats, 0); // version
        MMemWriteU16BE(&stats, 1); // type

        for (int frameOffset = FRAME_STEP; frameOffset < numFrames; frameOffset += FRAME_STEP) {
            MLogf("Profiling frame %d...", frameOffset);
            u32 rasterTimes[SAMPLES_REPEAT];
            u32 renderTimes[SAMPLES_REPEAT];
            for (int i = 0; i < SAMPLES_REPEAT; i++) {
                sCurrentClock = AOS_GetClockCount();
                RenderIntroAtTime(&sIntro, &introSceneSetup, frameOffset, FALSE, sCurrentClock);
                rasterTimes[i] = sRasterTime;
                renderTimes[i] = sRenderTime;
            }

            u32 avgRender = 0;
            u32 avgRaster = 0;
            for (int i = 0; i < SAMPLES_REPEAT; i++) {
                avgRender += renderTimes[i];
                avgRaster += rasterTimes[i];
            }

            avgRender /= SAMPLES_REPEAT;
            avgRaster /= SAMPLES_REPEAT;

            MMemWriteU32BE(&stats, frameOffset);
            MMemWriteU32BE(&stats, avgRender);
            MMemWriteU32BE(&stats, avgRaster);
        }
    } else {
        if (sProfile) {
            MMemWriteU16BE(&stats, 0); // version
            MMemWriteU16BE(&stats, 2); // type
        }

        AOS_screen_init();

        struct RastPort* rastPort = &aosScreen->RastPort;

        LoadRGB4(&aosScreen->ViewPort, sFIntroPalette, 256);

        if (sModToPlay) {
            // Stack the 50 Hz timer that plays the music
            StartAudioTickTimer(&sAudioContext);

            // Fast-forward to specific point - used for debugging
            if (sFrameOffset) {
                sCurrentClock = AOS_GetClockCount();
                u64 delta = Intro_GetTimeForFrameOffset(&sIntro, sFrameOffset);
                sStartTime = sCurrentClock - (((delta + 1) * sClockTickInterval) / 100);
                Audio_ModStartAt(&sAudioContext, sModToPlay, (delta + 1) / 2);
            }
        }

        b32 isDone = FALSE;
        while (AOS_processEvents() && !isDone) {
            fpsFrames++;
            totalFrames++;

            sCurrentClock = AOS_GetClockCount();
            ULONG elapsed = ((ULONG)(sCurrentClock - prevClock)) / (sClockTickInterval / 1000);
            prevClock = sCurrentClock;
            updateFpsTimer += elapsed;
            if (updateFpsTimer > 1000) {
                fps = fpsFrames - 1;
                fpsFrames = 0;
                updateFpsTimer = updateFpsTimer - 1000;

                // If rendering too many frames slow down using WaitTOF()
                if (fps > 200) {
                    sWaitTOF = TRUE;
                }
            }

            if (sWaitTOF) {
                WaitTOF();
                sCurrentClock = AOS_GetClockCount();
            }

            u64 deltaIn100thsOfaSecond = (sCurrentClock - sStartTime) / (sClockTickInterval / 100);
            if (!sPause) {
                sFrameOffset = Intro_GetFrameOffsetAtTime(&sIntro, deltaIn100thsOfaSecond);
            }

            if (sFrameOffset >= numFrames) {
                if (sProfile) {
                    isDone = TRUE;
                } else {
                    // Restart intro
                    sStartTime = sCurrentClock;
                    sFrameOffset = 0;
                    Audio_ModStart(&sAudioContext, sModToPlay);
                }
            }

            // Mod will loop if we don't switch to silence when it is done
            if (sModToPlay && Audio_ModDone(&sAudioContext)) {
                Audio_ModStart(&sAudioContext, Audio_ModEnum_SILENCE);
            }

            if (!sPause || sRender) {
                RenderIntroAtTime(&sIntro, &introSceneSetup, sFrameOffset, FALSE, sCurrentClock);
                sRender = FALSE;
            }

            if (sDebugMode && fps > 0) {
                snprintf(frameRateString, 100, "fps: %d frame: %d total: %05d raster: %05d render: %05d copy: %05d pal: %05d",
                         fps, sFrameOffset, sTotalTime, sRasterTime, sRenderTime, sCopyTime, sPalTime);

                Vec2i16 pos = { 2, 2 };
                Render_DrawBitmapText(&introSceneSetup, frameRateString, pos, 0x7, TRUE);
            }

            u8* buffer = NULL;
            ULONG bytesPerRow = 0;
            ULONG pixelFormat = 0;

            u64 startCopyClock = AOS_GetClockCount();

            APTR handle = LockBitMapTags(rastPort->BitMap,
                                         LBMI_BASEADDRESS, (ULONG) &buffer,
                                         LBMI_BYTESPERROW, (ULONG) &bytesPerRow,
                                         LBMI_PIXFMT, (ULONG) &pixelFormat,
                                         TAG_DONE);

            if (handle && buffer) {
                if (pixelFormat != PIXFMT_LUT8) {
                    UnLockBitMap(handle);
                    MLogf("Pixel format not supported: %d", pixelFormat);
                    AOS_cleanupAndExit(0);
                }

                u8* bufferLineDest = buffer;
                u8* bufferLineSrc = surface.pixels;
                for (int i = 0; i < SURFACE_HEIGHT; i++) {
                    memcpy(bufferLineDest, bufferLineSrc, SURFACE_WIDTH);

                    bufferLineSrc += SURFACE_WIDTH;
                    bufferLineDest += bytesPerRow;
                }

                UnLockBitMap(handle);
                u64 copyClock = AOS_GetClockCount();
                sCopyTime = ((copyClock - startCopyClock) * 10000) / sClockTickInterval;
            } else {
                MLog("missed frame");
            }

            u64 endClock = AOS_GetClockCount();
            sTotalTime = ((endClock - sCurrentClock) * 10000) / sClockTickInterval;

            if (sProfile) {
                MMemWriteU32BE(&stats, totalFrames);
                MMemWriteU32BE(&stats, sFrameOffset);
                MMemWriteU32BE(&stats, sTotalTime);
                MMemWriteU32BE(&stats, sRasterTime);
                MMemWriteU32BE(&stats, sRenderTime);
                MMemWriteU32BE(&stats, sCopyTime);
                MMemWriteU32BE(&stats, sPalTime);
            }
        }

        if (sModToPlay) {
            StopAudioTickTimer();
        }
    }

    if (sModToPlay) {
        Audio_Exit(&sAudioContext);
    }

    Raster_Free(&raster);
    Surface_Free(&surface);
    Render_Free(&introSceneSetup);
    Assets_FreeAmigaFiles(&assetsDataAmiga);

    MArrayFree(overrideModels);
    if (overridesFile.data) {
        MFree(overridesFile.data, overridesFile.size);
    }

    Intro_Free(&sIntro, &introSceneSetup);

    if (sProfile) {
        struct timeval currentTime;
        GetSysTime(&currentTime);

        char filename[128];
        snprintf(filename, sizeof(filename), "profile/%d-%d", currentTime.tv_secs, totalFrames);
        MFile fileOut = MFileWriteOpen(filename);

        u32 buildInfoLen = MStrLen(BUILD_STRING);
        MFileWriteData(&fileOut, (u8*)BUILD_STRING, buildInfoLen);

        MFileWriteData(&fileOut, (u8*)build_source_diffs, build_source_diffs_len);
        MFileWriteMem(&fileOut, &stats);
        MFileClose(&fileOut);
        MMemFree(&stats);
    }

    AOS_cleanupAndExit(0);

    return 0;
}
