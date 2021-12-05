#ifndef FINTRO_FINTRO_H
#define FINTRO_FINTRO_H

#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sIntroScene {
    u16 frameEnd;
    u16 frameStart;
    u32 dataOffset;
    u32 duration;
} IntroScene;

typedef struct sIntro {
    u8* sceneSetupData;
    u16* creditsStringData;
    u32 numScenes;
    IntroScene* scenes;
    ImageStore imageStore;
    u16 drawFrontierLogo;
} Intro;

typedef struct sScenePos {
    i32 scene;
    i32 offset;
} ScenePos;

void Intro_InitPC(Intro* intro, SceneSetup* sceneSetup, AssetsDataPC* assetsData);
void Intro_InitAmiga(Intro* intro, SceneSetup* sceneSetup, AssetsDataAmiga* assetsData);
void Intro_Free(Intro* intro, SceneSetup* sceneSetup);

void Intro_SetSceneForFrameOffset(Intro* intro, SceneSetup* sceneSetup, i32 frameOffset);
void Intro_Post3dRender(Intro* intro, SceneSetup* sceneSetup, i32 frameOffset);

u32 Intro_GetNumFrames(Intro* intro);
ScenePos Intro_GetScenePos(Intro* intro, u32 frameOffset);
u32 Intro_GetSceneFrameOffset(Intro* intro, u32 scene);
u32 Intro_GetFrameOffset(Intro* intro, ScenePos* scenePos);
u32 Intro_GetFrameOffsetAtTime(Intro* intro, u64 time);
u64 Intro_GetTimeForFrameOffset(Intro* intro, u32 frameOffset);

#ifdef __cplusplus
}
#endif

#endif