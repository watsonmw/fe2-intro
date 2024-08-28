#ifndef FINTRO_MODELCODE_H
#define FINTRO_MODELCODE_H

#include "mlib.h"
#include "fmath.h"
#include "assets.h"

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
    u8 codeOffsets;
    u8 hexCodeComment;
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
    u32 errorLen;
    b32 staticError;
    i32 errorLine;
    i32 errorColumn;
    u16 modelIndex;
} ModelCompileResult;

// Compile a single model file
ModelCompileResult CompileModel(const char* dataIn, u32 sizeIn, MMemIO* memOutput);

// Write out model offsets and binary to text file (this can then be loaded later by the override loading code)
i32 WriteModels(const char* filename, ModelsArray* models, MMemIO* modelsMem);

typedef enum {
    ModelEndian_LITTLE,
    ModelEndian_BIG,
} ModelEndianEnum;

// Compile multiple models and store the results in the given memory and model pointer list
// dumpModelsToConsole - Optionally decompile (round trip) for debugging purposes.
// endian - Endianness to write the models with
ModelCompileResult CompileMultipleModels(const char* dataIn, u32 sizeIn, MMemIO* memOutput, ModelsArray* outModels,
                                         ModelEndianEnum endian, b32 dumpModelsToConsole);

// Compile multiple models from file and write binary compiled code to given file
// The model pointers and memory models are available for direct use if compiled to same endian as host.
// Must be free'd after use.
i32 CompileAndWriteModels(const char* modelsFile, const char* outputFile, MMemIO* outModelMem, ModelsArray* outModels,
                          ModelEndianEnum endian, b32 dumpModelsToConsole);

void ModelCompile_Test();

#ifdef __cplusplus
}
#endif

#endif
