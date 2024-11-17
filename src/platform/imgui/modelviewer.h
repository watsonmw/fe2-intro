#ifndef FINTRO_MODEL_VIEWER_H
#define FINTRO_MODEL_VIEWER_H

#include "render.h"

typedef struct sModelViewer {
    SceneSetup sceneSetup;
    int modelIndex;
    RenderEntity entity;
    i32 yaw;
    i32 pitch;
    i32 roll;
    Vec3i32 pos;
    int posScale;
    int lightingAngleA;
    int lightingAngleB;
    int depthScale;
    int renderDetail;
    int planetDetail;
    int planetMinAtmosBandWidth;
} ModelViewer;

void ModelViewer_Init(ModelViewer* viewer);
void ModelViewer_InitAmiga(ModelViewer* viewer, AssetsData* assetsData);
void ModelViewer_ResetForModel(ModelViewer* viewer);
bool ModelViewer_ResetPosScale(ModelViewer* viewer);
bool ModelViewer_SetModelForScene(ModelViewer* viewer, i32 modelOffset);
bool ModelViewer_UpdateScene(ModelViewer* viewer);
void ModelViewer_Free(ModelViewer* viewer);

#endif