#include "modelviewer.h"
#include "modelcode.h"

void ModelViewer_Init(ModelViewer* viewer) {
    RenderEntity* entity = &viewer->entity;
    Entity_Init(entity);

    SceneSetup* sceneSetup = &viewer->sceneSetup;
    SceneSetup_InitDefaultShadeRamp(sceneSetup);

    viewer->pitch = 0;
    viewer->yaw = 0;
    viewer->roll = 0;

    viewer->pos[0] = 0;
    viewer->pos[1] = 0;
    viewer->pos[2] = 0;
    viewer->posScale = 0;

    // Light from top right front
    viewer->lightingAngleA = 7540;
    viewer->lightingAngleB = 6400;

    viewer->planetMinAtmosBandWidth = 0x4000;
    viewer->renderDetail = 2;
    viewer->planetDetail = 1;
}

void ModelViewer_InitAmiga(ModelViewer* viewer, AssetsData* assetsData) {
    ModelViewer_Init(viewer);

    SceneSetup* sceneSetup = &viewer->sceneSetup;
    RenderEntity* entity = &viewer->entity;

    u8* fileData = assetsData->mainExeData;

    Assets_LoadAmigaMainModels(assetsData);
    sceneSetup->assets.models = assetsData->mainModels;

    sceneSetup->debug.modelDataFileStartAddress = fileData;
    sceneSetup->debug.fontModelDataFileStartAddress = fileData;
    sceneSetup->assets.fonts = assetsData->galmapModels;
    sceneSetup->assets.bitmapFontData = assetsData->bitmapFontData;
    sceneSetup->assets.mainStrings = assetsData->mainStrings;
    sceneSetup->moduleStrings = sceneSetup->assets.mainStrings;

    memcpy(entity->entityText, "  REXLA", 8);
}

void ModelViewer_Free(ModelViewer* viewer) {
}

void ModelViewer_ResetForModel(ModelViewer* viewer) {
    viewer->pitch = 0;
    viewer->yaw = 0;
    viewer->roll = 0;

    // Light from top right front
    viewer->lightingAngleA = 7540;
    viewer->lightingAngleB = 6400;

    viewer->planetMinAtmosBandWidth = 0x4000;
    viewer->renderDetail = 2;
    viewer->planetDetail = 1;

    SceneSetup* sceneSetup = &viewer->sceneSetup;
    RenderEntity* entity = &viewer->entity;

    entity->depthScale = viewer->depthScale;
    ModelData* modelData = Render_GetModel(sceneSetup, viewer->modelIndex);
    if (!modelData) {
        return;
    }

    ModelViewer_ResetPosScale(viewer);

    i32 modelScale = (i32)modelData->scale1 + modelData->scale2 - entity->depthScale;
    i32 rScale = modelScale;
    i32 offsetZ = (modelData->radius * 3);
    if (rScale > 0) {
        offsetZ <<= rScale;
    } else if (rScale < 0) {
        offsetZ >>= -rScale;
    }
    viewer->pos[0] = 0;
    viewer->pos[1] = 0;
    viewer->pos[2] = 0;
    entity->entityPos[0] = 0;
    entity->entityPos[1] = 0;
    entity->entityPos[2] = offsetZ;
}

bool ModelViewer_ResetPosScale(ModelViewer* viewer) {
    SceneSetup* sceneSetup = &viewer->sceneSetup;
    RenderEntity* entity = &viewer->entity;
    ModelData* modelData = Render_GetModel(sceneSetup, viewer->modelIndex);
    if (!modelData) {
        return false;
    }

    entity->depthScale = viewer->depthScale;
    if (modelData->scale2 > 0) {
        entity->depthScale += 7 + modelData->scale2;
    }

    i32 modelScale = (i32)modelData->scale1 + modelData->scale2 - entity->depthScale;
    viewer->posScale = modelScale;

    return true;
}

bool ModelViewer_SetModelForScene(ModelViewer* viewer, i32 modelOffset) {
    RenderEntity* entity = &viewer->entity;
    viewer->modelIndex = modelOffset;
    entity->modelIndex = modelOffset;
    return ModelViewer_ResetPosScale(viewer);
}

bool ModelViewer_UpdateScene(ModelViewer* viewer) {
    SceneSetup* sceneSetup = &viewer->sceneSetup;
    RenderEntity* entity = &viewer->entity;

    ModelData* modelData = Render_GetModel(sceneSetup, viewer->modelIndex);
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

    i32 posScale = viewer->posScale;

    i32 x = viewer->pos[0];
    i32 y = viewer->pos[1];
    i32 z = viewer->pos[2];

    i32 zScale = posScale - 5;
    if (zScale > 0) {
        z <<= zScale;
    } else if (zScale < 0) {
        z >>= -zScale;
    }

    i32 pScale = posScale - 3;
    if (pScale > 0) {
        x <<= pScale;
        y <<= pScale;
    } else if (pScale < 0) {
        x >>= -pScale;
        y >>= -pScale;
    }

    i32 modelScale = (i32)modelData->scale1 + modelData->scale2 - entity->depthScale;
    i32 rScale = modelScale;
    i32 offsetZ = (modelData->radius * 3);
    if (rScale > 0) {
        offsetZ <<= rScale;
    } else if (rScale < 0) {
        offsetZ >>= -rScale;
    }
    entity->entityPos[0] = x;
    entity->entityPos[1] = y;
    entity->entityPos[2] = z + offsetZ;

    sceneSetup->renderDetail = viewer->renderDetail;
    sceneSetup->planetDetail = viewer->planetDetail;

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

    i16 lSinA = 0;
    i16 lCosA = 0;
    LookupSineAndCosine(viewer->lightingAngleA & 0xffff, &lSinA, &lCosA);
    i16 lSinB = 0;
    i16 lCosB = 0;
    LookupSineAndCosine(viewer->lightingAngleB & 0xffff, &lSinB, &lCosB);

    sceneSetup->lightDirView[0] = (i16)(((i32)lCosA * -lCosB) >> 15);
    sceneSetup->lightDirView[1] = (i16)(((i32)lSinA * -lCosB) >> 15);
    sceneSetup->lightDirView[2] = (i16)lSinB;

    sceneSetup->planetMinAtmosBandWidth = viewer->planetMinAtmosBandWidth;

    return true;
}