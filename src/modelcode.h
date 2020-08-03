#ifndef FINTRO_MODELCODE_H
#define FINTRO_MODELCODE_H

#include <stdint.h>

#include "mlib.h"
#include "fmath.h"
#include "assets.h"
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

MARRAY_TYPEDEF(u32, ModelIndicesArray)
typedef struct sByteCodeTrace {
    u32 index;
    u16 result;
} ByteCodeTrace;

MARRAY_TYPEDEF(ByteCodeTrace, ByteCodeTraceArray)

typedef struct sDebugModelInfo {
    u16 numVertices;
    u16 numNormals;
    b32 referencesParent;

    ModelIndicesArray modelIndexes;
} DebugModelInfo;

typedef struct sDebugModelParams {
    u64 offsetBegin;
    u32 maxSize;
    u32 onlyLabels;
    ByteCodeTraceArray* byteCodeTrace;
} DebugModelParams;

typedef enum eModelType {
    ModelType_OBJ,
    ModelType_FONT,
} ModelType;

void DecompileModel(ModelData* model, u32 modelIndex, DebugModelParams* debugModelParams,
        DebugModelInfo* modelInfo, MMemIO* strOutput);

void DecompileFontModel(FontModelData* model, u32 modelIndex, DebugModelParams* debugModelParams,
        DebugModelInfo* modelInfo, MMemIO* strOutput);

void DecompileModelToConsole(ModelData* modelData, u32 modelIndex, ModelType modelType);

typedef struct sModelCompileResult {
    u32 modelStartOffset;
    char* error;
    b32 staticError;
    i32 errorLine;
    i32 errorColumn;
    u16 modelIndex;
} ModelCompileResult;

ModelCompileResult CompileModel(const char* dataIn, u32 sizeIn, MMemIO* memOutput);

i32 CompileAndLoadModelOverrides(const char* filename, SceneSetup* sceneSetup);

void ModelCompile_Test();

#ifdef __cplusplus
}
#endif

#endif
