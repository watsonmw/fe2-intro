#ifndef FINTRO_MODEL_VIEWER_H
#define FINTRO_MODEL_VIEWER_H

#include "render.h"

typedef struct sModelViewer {
    RenderEntity entity;
    SceneSetup sceneSetup;
    i32 yaw;
    i32 pitch;
    i32 roll;
    int depthScale;
    int renderDetail;
    int planetDetail;
    int planetMinAtmosBandWidth;
    Vec3i32 pos;
} ModelViewer;

void ModelViewer_InitPC(ModelViewer* viewer, AssetsDataPC* assetsData);
void ModelViewer_InitAmiga(ModelViewer* viewer, AssetsDataAmiga* assetsData);
bool ModelViewer_SetSceneForModel(ModelViewer* viewer, i32 modelOffset);
void ModelViewer_Move(ModelViewer* viewer, Vec3i32 d);
void ModelViewer_Free(ModelViewer* viewer);

#endif