//
// Reverse Engineered from:
//   [Frontier: Elite II © David Braben 1993 & 1994](https://en.wikipedia.org/wiki/Frontier:_Elite_II)
//

#include "fintro.h"
#include "render.h"

IntroScene Intro_SceneList[] = {
        { 0x200, 0x000, 0, 768 },  // Spinny Intro Text
        { 0x400, 0x000, 0, 2100 }, // Animate Logo
        { 0x100, 0x000, 0, 384 },  // Courier flyby
        { 0x200, 0x000, 0, 768 },  // Courier rotate 180
        { 0x400, 0x000, 0, 768 },  // Courier dive
        { 0x400, 0x026, 0, 1920 }, // Courier land
        { 0x200, 0x000, 0, 384 },  // Bridge & power station
        { 0x200, 0x000, 0, 384 },  // City scene 1
        { 0x200, 0x000, 0, 384 },  // City scene 2
        { 0x200, 0x000, 0, 384 },  // City scene 3
        { 0x200, 0x001, 0, 1150 }, // Eagles dive
        { 0x1c0, 0x070, 0, 576 },  // Eagles planet attack
        { 0x200, 0x040, 0, 1536 }, // Courier climb
        { 0x400, 0x000, 0, 768 },  // Courier escape
        { 0x400, 0x000, 0, 768 },  // Eagles space attack
        { 0x400, 0x000, 0, 768 },  // Courier hyper arrive
        { 0x400, 0x000, 0, 960 },  // Station flyby
        { 0x200, 0x000, 0, 768 },  // Eagles hyper arrive
        { 0x200, 0x000, 0, 384 },  // Eagles attack over ship
        { 0x1fe, 0x000, 0, 1152 }, // Courier attack over ship & kill eagle 1
        { 0x200, 0x000, 0, 576 },  // Courier attack eagle 2
        { 0x400, 0x001, 0, 2208 }, // Courier kill eagle 2
        { 0x800, 0x400, 0, 1728 }, // Rotate courier
};

static void SetupEntityFromRenderData(Intro* intro, SceneSetup* sceneSetup, RenderEntity* entity, u32 renderDataOffset) {
    u8* renderData = (u8*) (intro->sceneSetupData + renderDataOffset);
#ifdef FINTRO_INSPECTOR
    sceneSetup->debug.renderDataOffset = renderDataOffset;
    sceneSetup->debug.renderData = renderData;
#endif
    Entity_Init(entity);
    memcpy(entity->viewMatrix, renderData, sizeof(entity->viewMatrix));
    memcpy(entity->entityPos, renderData + 0x14, sizeof(entity->entityPos));
    u16* u16data = (u16*)(renderData);
    entity->modelIndex = u16data[0x2d] >> 1;
    memcpy(entity->entityVars, renderData + 0x72, sizeof(entity->entityVars));
}

void Intro_Init(Intro* intro) {
    intro->scenes = Intro_SceneList;
    intro->numScenes = sizeof(Intro_SceneList) / sizeof(IntroScene);
}

void Intro_InitAmiga(Intro* intro, SceneSetup* sceneSetup, AssetsData* assetsData) {
    Intro_Init(intro);

    intro->sceneSetupData = assetsData->mainExeData;
    intro->lastScene = -1;
    sceneSetup->planetMinAtmosBandWidth = 0x0;
    SceneSetup_InitDefaultShadeRamp(sceneSetup);

    Assets_LoadAmigaIntroModels(assetsData);
    sceneSetup->assets.models = assetsData->introModels;

    u8* fileData = assetsData->mainExeData;
    if (assetsData->assetsRead == AssetsRead_Amiga_Orig) {
        sceneSetup->moduleStrings = Assets_LoadStringPointers16BE(fileData + 0x76f88, 33);
        sceneSetup->moduleStringNum = 33 ;
        intro->creditsStringData = (u16*)(fileData + 0x81e16);
    } else if (assetsData->assetsRead == AssetsRead_Amiga_EliteClub ||
            assetsData->assetsRead == AssetsRead_Amiga_EliteClub2) {
        sceneSetup->moduleStrings = Assets_LoadStringPointers16BE(fileData + 0x7fe0e, 33);
        sceneSetup->moduleStringNum = 33;
        intro->creditsStringData = (u16*)(fileData + 0x8b774);
    }

    ARRAY_REWRITE_BE16((u8*)intro->creditsStringData, 26 * 2);

    sceneSetup->assets.fonts = assetsData->galmapModels;
    sceneSetup->assets.bitmapFontData = assetsData->bitmapFontData;
    sceneSetup->assets.mainStrings = assetsData->mainStrings;

    intro->scenes = Intro_SceneList;
    intro->numScenes = sizeof(Intro_SceneList) / sizeof(IntroScene);

    if (assetsData->assetsRead == AssetsRead_Amiga_Orig) {
        intro->scenes[0].dataOffset = 0x745c6;
        intro->scenes[1].dataOffset = 0x74462;
        intro->scenes[2].dataOffset = 0x747bc;
    } else {
        intro->scenes[0].dataOffset = 0x7d44c;
        intro->scenes[1].dataOffset = 0x7d2e8;
        intro->scenes[2].dataOffset = 0x7d642;
    }

    // Apply scene data endianness fixups
    for (u32 i = 0; i < intro->numScenes; ++i) {
        if (intro->scenes[i].dataOffset == 0) {
            intro->scenes[i].dataOffset = intro->scenes[i - 1].dataOffset + 0x150;
        }

        u8* data1 = assetsData->mainExeData + intro->scenes[i].dataOffset;
        ARRAY_REWRITE_BE16(data1, 0x14);
        ARRAY_REWRITE_BE32(data1 + 0x14, 0xc);
        ARRAY_REWRITE_BE16(data1 + 0x20, 0xd6);
    }

    sceneSetup->assets.mainData =  assetsData->mainExeData;

    MArrayInit(intro->imageStore.images);

    if (intro->drawFrontierLogo) {
#if FINTRO_SCREEN_RES == 3
        Image8Bit image;
        Image8Bit* imagePtr = &image;
#else
        Image8Bit* imagePtr = MArrayAddPtr(intro->imageStore.images);
#endif
        Render_ImageFromPlanerBitmap(imagePtr,
                                     sceneSetup->assets.mainData + 0x0007f2aa,
                                     (u16*) sceneSetup->assets.mainData + 0x0007f29a,
                                     16);
#if FINTRO_SCREEN_RES == 3
        Image8Bit* upScaledImage = MArrayAddPtr(intro->imageStore.images);
        Render_ImageUpscale2x(&image, upScaledImage);
        MFree(image.data, image.w * image.h);
#endif
    }
    sceneSetup->planetDetail = 2;
    sceneSetup->renderDetail = 2;
}

void Intro_Free(Intro* intro, SceneSetup* sceneSetup) {
    MFree(sceneSetup->moduleStrings, sceneSetup->moduleStringNum * sizeof(u8*));

    Images8Bit* images = &intro->imageStore.images;
    for (int i = 0; i < MArraySize(*images); i++) {
        Image8Bit* image = MArrayGetPtr(*images, i);
        MFree(image->data, image->w * image->h);
    }

    MArrayFree(*images);
}

u32 Intro_GetNumFrames(Intro* intro) {
    i32 offset = 0;

    u32 numScenes = intro->numScenes;

    for (u32 i = 0; i < numScenes; ++i) {
        IntroScene* introScene = intro->scenes + i;
        offset += introScene->frameEnd - introScene->frameStart;
    }

    return offset;
}

ScenePos Intro_GetScenePos(Intro* intro, u32 frameOffset) {
    ScenePos scenePos;
    scenePos.scene = -1;
    scenePos.offset = 0;

    u32 numScenes = intro->numScenes;

    i32 offset = frameOffset;

    for (u32 i = 0; i < numScenes; ++i) {
        IntroScene* introScene = intro->scenes + i;
        if (offset < (introScene->frameEnd - introScene->frameStart)) {
            scenePos.scene = i;
            scenePos.offset = offset;
            break;
        }
        offset -= (introScene->frameEnd - introScene->frameStart);
    }

    return scenePos;
}

u32 Intro_GetSceneFrameOffset(Intro* intro, u32 scene) {
    i32 offset = 0;

    u32 numScenes = intro->numScenes - 1;
    if (scene > numScenes) {
        scene = numScenes;
    }

    for (u32 i = 0; i < scene; ++i) {
        IntroScene* introScene = intro->scenes + i;
        offset += introScene->frameEnd - introScene->frameStart;
    }

    return offset;
}

u32 Intro_GetFrameOffset(Intro* intro, ScenePos* scenePos) {
    i32 frameOffset = 0;
    i32 sceneOffset = scenePos->scene;

    u32 numScenes = intro->numScenes - 1;
    if (sceneOffset > numScenes) {
        sceneOffset = numScenes;
    }

    for (u32 i = 0; i < sceneOffset; ++i) {
        IntroScene* introScene = intro->scenes + i;
        frameOffset += introScene->frameEnd - introScene->frameStart;
    }

    return frameOffset + scenePos->offset;
}

void Intro_SetSceneForFrameOffset(Intro* intro, SceneSetup* sceneSetup, RenderEntity* entity, int frameOffset) {
    ScenePos scenePos = Intro_GetScenePos(intro, frameOffset);
    if (scenePos.scene < 0) {
        scenePos.scene = 0;
        scenePos.offset = 0;
    }

    if (intro->lastScene != scenePos.scene) {
        SetupEntityFromRenderData(intro, sceneSetup, entity, intro->scenes[scenePos.scene].dataOffset);
        intro->lastScene = scenePos.scene;
    }
    entity->entityVars[0] = scenePos.offset + intro->scenes[scenePos.scene].frameStart;

    if (scenePos.scene == 1) {
        int lightAngle = ((scenePos.offset >> 1) & 0x1ff) - 0x140;
        if (lightAngle > 0) {
            if (lightAngle > 0x3f) {
                lightAngle = 0x3f;
            }
        } else {
            lightAngle = 0;
        }

        lightAngle = (lightAngle - 0x20) * 0x180;

        i16 sine = 0;
        i16 cosine = 0;
        u16 angle = (lightAngle & 0xffff);
        LookupSineAndCosine(angle, &sine, &cosine);

        sceneSetup->lightDirView[0] = (i16)((0x5a82 * sine) >> 16);
        sceneSetup->lightDirView[1] = (i16)0xa57e;
        sceneSetup->lightDirView[2] = (i16)((0x5a82 * cosine) >> 16);
    } else {
        sceneSetup->lightDirView[0] = (i16)0xb619;
        sceneSetup->lightDirView[1] = (i16)0xb619;
        sceneSetup->lightDirView[2] = (i16)0x49e7;

        switch (scenePos.scene) {
            case 10: {
                // Planet dive, fix light in relation to planet
                u16 rot = -(entity->entityVars[0] << 7);
                rot = 4096 + (rot >> 3);

                Matrix3x3i16 rotation;
                Matrix3x3i16Identity(rotation);
                Matrix3x3i16RotateAxisAngle(rotation, RotateAxis_Y, rot);

                Vec3i16 lightVec;
                MatrixMult_Vec3i16(sceneSetup->lightDirView, rotation, lightVec);
                Vec3i16Copy(lightVec, sceneSetup->lightDirView);
                break;
            }
            case 12: {
                // Planet courier lift-off, keep light pointed at planet
                u16 rot = (entity->entityVars[0] << 5) & 0x3fff;

                Matrix3x3i16 rotation;
                Matrix3x3i16Identity(rotation);
                Matrix3x3i16RotateAxisAngle(rotation, RotateAxis_X, (rot - 2048));

                Vec3i16 lightVec;
                MatrixMult_Vec3i16(sceneSetup->lightDirView, rotation, lightVec);
                Vec3i16Copy(lightVec, sceneSetup->lightDirView);
                break;
            }
            default:
                // Leave as it
                break;
        }
    }
}

u32 Intro_GetFrameOffsetAtTime(Intro* intro, u64 delta) {
    i32 offset = 0;
    for (u32 i = 0; i < intro->numScenes; ++i) {
        IntroScene* introScene = intro->scenes + i;
        u32 frameLen = introScene->frameEnd - introScene->frameStart;
        u32 duration = introScene->duration;
        if (delta < duration) {
            u32 frameOffset = (frameLen * delta) / duration;
            return offset + frameOffset;
        } else {
            delta -= duration;
        }

        offset += frameLen;
    }

    return offset;
}

u64 Intro_GetTimeForFrameOffset(Intro* intro, u32 frameOffset) {
    i32 delta = 0;
    for (u32 i = 0; i < intro->numScenes; ++i) {
        IntroScene* introScene = intro->scenes + i;
        u32 frameLen = introScene->frameEnd - introScene->frameStart;
        u32 duration = introScene->duration;
        if (frameOffset < frameLen) {
            u32 partialDuration = (duration * frameOffset) / frameLen;
            return delta + partialDuration;
        } else {
            frameOffset -= frameLen;
        }

        delta += duration;
    }

    return delta;
}

void Intro_Post3dRender(Intro* intro, SceneSetup* sceneSetup, i32 frameOffset) {
    ScenePos scenePos = Intro_GetScenePos(intro, frameOffset);
    if (scenePos.scene < 0) {
        scenePos.scene = 0;
        scenePos.offset = 0;
    }

    if (intro->drawFrontierLogo && scenePos.scene >= 2) {
        Image8Bit* image = MArrayGetPtr(intro->imageStore.images, 0);
        Vec2i16 pos;
        pos.x = (sceneSetup->raster->surface->width - image->w) / 2;
        pos.y = (sceneSetup->raster->surface->height - image->h);
        Render_BlitNoClip(sceneSetup->raster->surface, image, pos);
    }

    if (scenePos.scene >= 21) {
        u32 size = 2000;
        i8 buffer[size];

        u16 stringIndex;
        Vec2i16 pos;
        if (scenePos.scene == 21 && scenePos.offset > 800) {
            stringIndex = 23;
            pos.x = 30;
            pos.y = 15;
        } else if (scenePos.scene == 22) {
            if (scenePos.offset < 200) {
                stringIndex = 23;
                pos.x = 30;
                pos.y = 15;
            } else if (scenePos.offset < 600) {
                stringIndex = 24;
                pos.x = 10;
                pos.y = 55;
            } else {
                stringIndex = 25;
                pos.x = 10;
                pos.y = 1;
            }
        } else {
            return;
        }

        i8* text = (i8*)intro->creditsStringData + (intro->creditsStringData[stringIndex]);
        Render_ProcessString(sceneSetup, NULL, text, buffer, size);
        Render_DrawBitmapText(sceneSetup, buffer, pos, 0xf, TRUE);
    }
}
