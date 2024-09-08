#include "modelviewer.h"
#include "modelcode.h"

void ModelViewer_InitCommon(ModelViewer* viewer) {
    RenderEntity* entity = &viewer->entity;
    Entity_Init(entity);

    SceneSetup* sceneSetup = &viewer->sceneSetup;
    SceneSetup_InitDefaultShadeRamp(sceneSetup);

    sceneSetup->lightDirView[0] = 0xb619;
    sceneSetup->lightDirView[1] = 0xb619;
    sceneSetup->lightDirView[2] = 0x49e7;

    viewer->yaw = 0;
    viewer->roll = 0;
    viewer->pos[0] = 0;
    viewer->pos[1] = 0;
    viewer->pos[2] = 0;
    viewer->planetMinAtmosBandWidth = 0x4000;
}

void ModelViewer_InitPC(ModelViewer* viewer, AssetsDataPC* assetsData) {
    ModelViewer_InitCommon(viewer);

    RenderEntity* entity = &viewer->entity;
    SceneSetup* sceneSetup = &viewer->sceneSetup;

    Assets_LoadModelPointers16LE(assetsData->mainExeData + 0x418d4, 300, &sceneSetup->assets.models);
    Assets_LoadModelPointers16LE(assetsData->galmapModels, 15, &sceneSetup->assets.galmapModels);

    sceneSetup->moduleStrings = Assets_LoadStringPointers16LE(assetsData->mainStringData, 126);
    sceneSetup->assets.bitmapFontData = assetsData->bitmapFontData;

    memcpy(entity->entityText, "  RUMBA", 8);

    sceneSetup->modelDataFileStartAddress = assetsData->mainExeData;
    sceneSetup->fontModelDataFileStartAddress = assetsData->mainExeData;
}

void ModelViewer_InitAmiga(ModelViewer* viewer, AssetsDataAmiga* assetsData) {
    ModelViewer_InitCommon(viewer);

    SceneSetup* sceneSetup = &viewer->sceneSetup;
    RenderEntity* entity = &viewer->entity;

    u8* fileData = assetsData->mainExeData;

    if (assetsData->assetsRead == AssetsRead_Amiga_Orig) {
        // Load model data
        Assets_LoadModelPointers16BE(fileData + 0x28804, 300, &sceneSetup->assets.models);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x289e4, 0xffee);

        // Galaxy models
        Assets_LoadModelPointers32BE(fileData + 0x39ea0, 16,  &sceneSetup->assets.galmapModels);
    } else if (assetsData->assetsRead == AssetsRead_Amiga_EliteClub) {
        Assets_LoadModelPointers32BE(fileData + 0x28804, 300, &sceneSetup->assets.models);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x28c8c, 0x11698);
    } else if (assetsData->assetsRead == AssetsRead_Amiga_EliteClub2) {
        Assets_LoadModelPointers32BE(fileData + 0x28804, 300, &sceneSetup->assets.models);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x28c8c, 0x11698);

        ARRAY_REWRITE_BE16(fileData + 0x310fe, 92); // 61
        ARRAY_REWRITE_BE16(fileData + 0x31472, 398); // 63
        ARRAY_REWRITE_BE16(fileData + 0x31960, 142); // 65
        ARRAY_REWRITE_BE16(fileData + 0x31b4c, 92); // 69
        ARRAY_REWRITE_BE16(fileData + 0x31bf6, 76); // 70
        ARRAY_REWRITE_BE16(fileData + 0x31d58, 336); // 72
        ARRAY_REWRITE_BE16(fileData + 0x32236, 86); // 73
        ARRAY_REWRITE_BE16(fileData + 0x3234a, 72); // 74
        ARRAY_REWRITE_BE16(fileData + 0x32452, 16); // 75
        ARRAY_REWRITE_BE16(fileData + 0x324cc, 72); // 76
    }

    sceneSetup->modelDataFileStartAddress = fileData;
    sceneSetup->fontModelDataFileStartAddress = fileData;
    sceneSetup->assets.galmapModels = assetsData->galmapModels;
    sceneSetup->assets.bitmapFontData = assetsData->bitmapFontData;
    sceneSetup->assets.mainStrings = assetsData->mainStrings;
    sceneSetup->moduleStrings = sceneSetup->assets.mainStrings;

    memcpy(entity->entityText, "  REXLA", 8);
}

void ModelViewer_Free(ModelViewer* viewer) {
    MArrayFree(viewer->sceneSetup.assets.models);
}

bool ModelViewer_SetSceneForModel(ModelViewer* viewer, i32 modelOffset) {
    SceneSetup* sceneSetup = &viewer->sceneSetup;
    RenderEntity* entity = &viewer->entity;
    entity->modelIndex = modelOffset;

    ModelData* modelData = Render_GetModel(sceneSetup, modelOffset);
    if (modelData == NULL) {
        return false;
    }

    DebugModelInfo modelInfo;
    DebugModelParams params;
    memset(&params,  0, sizeof(DebugModelParams));
    params.maxSize = 0xfff;
    MMemIO writer;
    MMemInitAlloc(&writer, 100);
    DecompileModel(modelData, 0, &params, &modelInfo, &writer);
    MArrayFree(modelInfo.modelIndexes);
    MMemFree(&writer);
    if (modelInfo.referencesParent) {
        return false;
    }

    entity->depthScale = viewer->depthScale;
    if (modelData->scale2 > 0) {
        entity->depthScale = 7 + modelData->scale2;
    }
    sceneSetup->renderDetail = viewer->renderDetail;
    sceneSetup->planetDetail = viewer->planetDetail;

    i32 scale = (i32)modelData->scale1 + modelData->scale2 - entity->depthScale;

    i32 x = viewer->pos[0];
    i32 y = viewer->pos[1];
    i32 z = viewer->pos[2];

    i32 zScale = scale - 5;
    if (zScale > 0) {
        z <<= zScale;
    } else if (zScale < 0) {
        z >>= -zScale;
    }

    i32 pScale = scale - 3;
    if (pScale > 0) {
        x <<= pScale;
        y <<= pScale;
    } else if (pScale < 0) {
        x >>= -pScale;
        y >>= -pScale;
    }

    i32 rScale = scale;
    i32 offsetZ = (modelData->radius * 3);
    if (rScale > 0) {
        offsetZ <<= rScale;
    } else if (rScale < 0) {
        offsetZ >>= -rScale;
    }
    entity->entityPos[0] = x;
    entity->entityPos[1] = y;
    entity->entityPos[2] = z + offsetZ;

    Matrix3x3i16 m;
    i16 sinA = 0;
    i16 cosA = 0;
    LookupSineAndCosine(viewer->roll & 0xffff, &sinA, &cosA);

    i16 cosB = 0;
    i16 sinB = 0;
    LookupSineAndCosine(viewer->yaw & 0xffff, &sinB, &cosB);

    i16 sinY = 0;
    i16 cosY = 0;
    LookupSineAndCosine(viewer->pitch & 0xffff, &sinY, &cosY);

    m[0][0] = (i16)(((i32)cosA * -cosB) >> 15);
    m[0][1] = (i16)(((i32)sinA * -cosB) >> 15);
    m[0][2] = (i16)sinB;

    m[1][0] = (i16)(((((i32)cosA * -sinB) >> 15) * (((i32)sinY)) - ((i32)sinA * cosY))  >> 15);
    m[1][1] = (i16)(((((i32)sinA * -sinB) >> 15) * (((i32)sinY)) + ((i32)cosA * cosY))  >> 15);
    m[1][2] = (i16)(((i32)-cosB * sinY) >> 15);

    m[2][0] = (i16)(((((i32)cosA * -sinB) >> 15) * (((i32)cosY)) + ((i32)sinA * sinY))  >> 15);
    m[2][1] = (i16)(((((i32)sinA * -sinB) >> 15) * (((i32)cosY)) - ((i32)cosA * sinY))  >> 15);
    m[2][2] = (i16)(((i32)-cosB * cosY) >> 15);

    Matrix3i16Copy(m, entity->viewMatrix);

    sceneSetup->planetMinAtmosBandWidth = viewer->planetMinAtmosBandWidth;
    return true;
}

void ModelViewer_Move(ModelViewer* viewer, Vec3i32 d) {
    viewer->pos[0] -= d[0];
    viewer->pos[1] -= d[1];
    viewer->pos[2] -= d[2];
}
