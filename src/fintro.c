//
// Reverse Engineered from:
//   [Frontier: Elite II © David Braben 1993 & 1994](https://en.wikipedia.org/wiki/Frontier:_Elite_II)
//

#include "fintro.h"
#include "render.h"

static IntroScene Intro_SceneList[] = {
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
        { 0x400, 0x040, 0, 2208 }, // Courier kill eagle 2
        { 0x800, 0x400, 0, 1728 }, // Rotate courier
};

static void SetupSceneFromRenderData(Intro* intro, SceneSetup* sceneSetup, u32 renderDataOffset) {
    u8* renderData = (u8*) (intro->sceneSetupData + renderDataOffset);
#ifdef FINSPECTOR
    sceneSetup->renderDataOffset = renderDataOffset;
#endif
    u16* data = (u16*)(renderData);
    sceneSetup->modelIndex = data[0x2d] >> 1;

#ifdef FINSPECTOR
    sceneSetup->modelDataFileStartAddress = intro->sceneSetupData;
#endif

    memcpy(sceneSetup->modelVars, renderData + 0x72, sizeof(sceneSetup->modelVars));
    memcpy(sceneSetup->modelText, renderData + 0xf6, sizeof(sceneSetup->modelText));
    memcpy(sceneSetup->viewMatrix, renderData, 0x20);
}

void Intro_InitPC(Intro* intro, SceneSetup* sceneSetup, AssetsDataPC* assetsData) {
    u8* introFileData = assetsData->introModuleData + 0x200;
    u8* gameFileData = assetsData->gameMenuModuleData + 0x200;

    intro->sceneSetupData = introFileData;
    intro->creditsStringData = (u16*)(gameFileData + 0xa3);

#ifdef FINSPECTOR
    sceneSetup->galmapModelDataFileStartAddress = assetsData->mainExeData;
    sceneSetup->modelDataFileStartAddress = assetsData->mainExeData;
#endif

    sceneSetup->moduleStrings = Assets_LoadStringPointers16LE(introFileData + 0x1d8, 33);
    sceneSetup->moduleStringNum = 33;

    Assets_LoadModelPointers16LE(introFileData + 0x3b0, 120, &sceneSetup->assets.models);
    Assets_LoadModelPointers16LE(assetsData->vectorFontData, 15, &sceneSetup->assets.galmapModels);

    sceneSetup->assets.bitmapFontData = assetsData->bitmapFontData;

    sceneSetup->renderPlanetAtmos = 0xfff;

    sceneSetup->shadeRamp[0] = 0x0777;
    sceneSetup->shadeRamp[1] = 0x0777;
    sceneSetup->shadeRamp[2] = 0x0666;
    sceneSetup->shadeRamp[3] = 0x0555;
    sceneSetup->shadeRamp[4] = 0x0444;
    sceneSetup->shadeRamp[5] = 0x0333;
    sceneSetup->shadeRamp[6] = 0x0222;
    sceneSetup->shadeRamp[7] = 0x0111;

    intro->scenes = Intro_SceneList;

    u32 i = 0;
    intro->scenes[i++].dataOffset = 0x6f4b;
    intro->scenes[i++].dataOffset = 0x00a3;
    intro->scenes[i++].dataOffset = 0x7155;
    intro->scenes[i++].dataOffset = 0x72bb;
    intro->scenes[i++].dataOffset = 0x7421;
    intro->scenes[i++].dataOffset = 0x7587;
    intro->scenes[i++].dataOffset = 0x76ed;
    intro->scenes[i++].dataOffset = 0x7853;
    intro->scenes[i++].dataOffset = 0x79b9;
    intro->scenes[i++].dataOffset = 0x7b1f;
    intro->scenes[i++].dataOffset = 0x7C85;
    intro->scenes[i++].dataOffset = 0x7DEB;
    intro->scenes[i++].dataOffset = 0x7F51;
    intro->scenes[i++].dataOffset = 0x80B7;
    intro->scenes[i++].dataOffset = 0x821D;
    intro->scenes[i++].dataOffset = 0x8383;
    intro->scenes[i++].dataOffset = 0x84E9;
    intro->scenes[i++].dataOffset = 0x864F;
    intro->scenes[i++].dataOffset = 0x87B5;
    intro->scenes[i++].dataOffset = 0x891B;
    intro->scenes[i++].dataOffset = 0x8A81;
    intro->scenes[i++].dataOffset = 0x8BE7;
    intro->scenes[i++].dataOffset = 0x8D4D;

    intro->numScenes = sizeof(Intro_SceneList) / sizeof(IntroScene);

    // Apply scene data endianness fixups
    for (i = 0; i < intro->numScenes; ++i) {
        u8* data1 = introFileData + intro->scenes[i].dataOffset;
        ARRAY_REWRITE_LE16(data1, 0x14);
        ARRAY_REWRITE_LE32(data1 + 0x14, 0xc);
        ARRAY_REWRITE_LE16(data1 + 0x20, 0xd6);
    }

    MArrayInit(intro->imageStore.images);


    intro->drawFrontierLogo = 0;
}

void Intro_InitAmiga(Intro* intro, SceneSetup* sceneSetup, AssetsDataAmiga* assetsData) {
    intro->sceneSetupData = assetsData->mainExeData;

    sceneSetup->renderPlanetAtmos = 0xfff;

    sceneSetup->shadeRamp[0] = 0x0777;
    sceneSetup->shadeRamp[1] = 0x0777;
    sceneSetup->shadeRamp[2] = 0x0666;
    sceneSetup->shadeRamp[3] = 0x0555;
    sceneSetup->shadeRamp[4] = 0x0444;
    sceneSetup->shadeRamp[5] = 0x0333;
    sceneSetup->shadeRamp[6] = 0x0222;
    sceneSetup->shadeRamp[7] = 0x0111;

    u8* fileData = assetsData->mainExeData;

    if (assetsData->assetsRead == AssetsRead_Amiga_Orig) {
        // Load model data
        u8* modelDataStart = fileData + 0x77160;
        Assets_LoadModelPointers16BE(modelDataStart, 120, &sceneSetup->assets.models);
        ARRAY_REWRITE_BE16(fileData + 0x387d6, 0x18fc);

        u32 planetByteCodeLen = 28;
        u32 planetByteCodeStart = 0x2772;
        ARRAY_REWRITE_BE16(modelDataStart, planetByteCodeStart);
        ARRAY_REWRITE_BE16((modelDataStart + planetByteCodeStart + planetByteCodeLen),
                           (0x6a34 - (planetByteCodeStart + planetByteCodeLen)));

        sceneSetup->moduleStrings = Assets_LoadStringPointers16BE(fileData + 0x76f88, 33);
        sceneSetup->moduleStringNum = 33 ;
        intro->creditsStringData = (u16*)(fileData + 0x81e16);
    } else if (assetsData->assetsRead == AssetsRead_Amiga_EliteClub) {
        Assets_LoadModelPointers32BE(fileData + 0x7ffdc, 120, &sceneSetup->assets.models);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x80464, 0x687c);

        // Flip back planet byte code
        ARRAY_REWRITE_BE16(fileData + 0x829f8, 28);

        sceneSetup->moduleStrings = Assets_LoadStringPointers16BE(fileData + 0x7fe0e, 33);
        sceneSetup->moduleStringNum = 33;
        intro->creditsStringData = (u16*)(fileData + 0x8b774);
    } else if (assetsData->assetsRead == AssetsRead_Amiga_EliteClub2) {
        Assets_LoadModelPointers32BE(fileData + 0x7ffdc, 120, &sceneSetup->assets.models);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x80464, 0x687c);

        // Flip back planet byte code
        ARRAY_REWRITE_BE16(fileData + 0x829f8, 28);

        sceneSetup->moduleStrings = Assets_LoadStringPointers16BE(fileData + 0x7fe0e, 33);
        sceneSetup->moduleStringNum = 33;
        intro->creditsStringData = (u16*)(fileData + 0x8b774);
    }

    ARRAY_REWRITE_BE16((u8*)intro->creditsStringData, 26 * 2);

#ifdef FINSPECTOR
    sceneSetup->modelDataFileStartAddress = fileData;
    sceneSetup->galmapModelDataFileStartAddress = fileData;
#endif

    sceneSetup->assets.galmapModels = assetsData->galmapModels;
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
}

void Intro_Free(Intro* intro, SceneSetup* sceneSetup) {
    MArrayFree(sceneSetup->assets.models);
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

void Intro_SetSceneForFrameOffset(Intro* intro, SceneSetup* sceneSetup, int frameOffset) {
    ScenePos scenePos = Intro_GetScenePos(intro, frameOffset);
    if (scenePos.scene < 0) {
        scenePos.scene = 0;
        scenePos.offset = 0;
    }

    SetupSceneFromRenderData(intro, sceneSetup, intro->scenes[scenePos.scene].dataOffset);
    sceneSetup->modelVars[0] = scenePos.offset + intro->scenes[scenePos.scene].frameStart;

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

        sceneSetup->lightVec[0] = (i16)((0x5a82 * sine) >> 16);
        sceneSetup->lightVec[1] = (i16)0xa57e;
        sceneSetup->lightVec[2] = (i16)((0x5a82 * cosine) >> 16);
    } else {
        sceneSetup->lightVec[0] = (i16)0xb619;
        sceneSetup->lightVec[1] = (i16)0xb619;
        sceneSetup->lightVec[2] = (i16)0x49e7;

        switch (scenePos.scene) {
            case 10: {
                // Planet dive, fix light in relation to planet
                u16 rot = -(sceneSetup->modelVars[0] << 7);
                rot = 4096 + (rot >> 3);

                Matrix3x3i16 rotation;
                Matrix3x3i16Identity(rotation);
                Matrix3x3i16RotateAxisAngle(rotation, RotateAxis_Y, -rot);

                Vec3i16 lightV;
                MultVec3i16Matrix(rotation, sceneSetup->lightVec, lightV);
                Vec3i16Copy(lightV, sceneSetup->lightVec);
                break;
            }
            case 12: {
                // Planet courier lift-off, keep light pointed at planet
                u16 rot = (sceneSetup->modelVars[0] << 5) & 0x3fff;

                Matrix3x3i16 rotation;
                Matrix3x3i16Identity(rotation);
                Matrix3x3i16RotateAxisAngle(rotation, RotateAxis_X, -(rot - 2048));

                Vec3i16 lightV;
                MultVec3i16Matrix(rotation, sceneSetup->lightVec, lightV);
                Vec3i16Copy(lightV, sceneSetup->lightVec);
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
        Render_ProcessString(sceneSetup, text, buffer, size);
        Render_DrawBitmapText(sceneSetup, buffer, pos, 0xf, TRUE);
    }
}
