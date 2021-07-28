#include "modelcode.h"

#include <stdio.h>
#include <string.h>

// Decompile / Compile FE2 models
// The text format here is our own invention and uses a custom parser.
// It will compile model code loadable by the original version of the game.

static inline u16 ror16(u16 n, u8 c) {
    u16 mask = ((sizeof(u16) * 8) - 1);
    c &= mask;
    return (n >> c) | (n << ((-c)&mask));
}

static void PrintParam8Base10(char* buff, int buffSize, u16 param) {
    u16 cmd = param & 0xc0;
    u16 val = param & 0x3f;

    switch (cmd) {
        case 0x00:
            snprintf(buff, buffSize, "%d", (int)val);
            break;
        case 0x40:
            snprintf(buff, buffSize, "%d", (int)(val << 10));
            break;
        case 0x80:
            snprintf(buff, buffSize, "scene[%d]", (int)val);
            break;
        case 0xc0:
        default:
            snprintf(buff, buffSize, "tmp[%d]", (int)val);
            break;
    }
}

static void PrintParam8HexFract(char *buff, int buffSize, u16 param) {
    u16 cmd = param & 0xc0;
    u16 val = param & 0x3f;

    switch (cmd) {
        case 0x00:
            snprintf(buff, buffSize, ".%04x", val);
            break;
        case 0x40:
            snprintf(buff, buffSize, ".%04x", val << 10);
            break;
        case 0x80:
            snprintf(buff, buffSize, "scene[%d]", val);
            break;
        case 0xc0:
        default:
            snprintf(buff, buffSize, "tmp[%d]", val);
            break;
    }
}

static void PrintParam16Base10(char* buff, int buffSize, u16 param16) {
    if (param16 & 0x80) {
        u16 val = param16 & 0x3f;
        if (param16 & 0x40) {
            // this can alias values off the end of this struct?
            snprintf(buff, buffSize, "tmp[%d]", val);
        } else {
            snprintf(buff, buffSize, "scene[%d]", val);
        }
        return;
    }

    u16 val = (param16 << 9) | (param16 >> 7);
    snprintf(buff, buffSize, "%d", val);
}

static void PrintBallSize(char* buff, int buffSize, u16 param) {
    u16 cmd = param & 0xc0;
    u16 val = param & 0x3f;

    switch (cmd) {
        case 0x00:
        case 0x40: {
            param = ror16(param, 7);
            if (param & 0x8000) {
                snprintf(buff, buffSize, "fixed:%d", param ^ 0x8000);
            } else {
                snprintf(buff, buffSize, "size:%d", param);
            }
            break;
        }
        case 0x80:
            snprintf(buff, buffSize, "size:scene[%d]", val);
            break;
        case 0xc0:
        default:
            snprintf(buff, buffSize, "size:tmp[%d]", val);
            break;
    }
}

ByteCodeTrace* TraceForOffset(ByteCodeTraceArray* byteCodeTrace, int insOffset) {
    for (int i = 0; i < MArraySize(*byteCodeTrace); i++) {
        ByteCodeTrace* trace = MArrayGetPtr(*byteCodeTrace, i);
        if (trace->index == insOffset) {
            return trace;
        }
    }
    return NULL;
}

static void DumpVerticesAndNormals(ModelData* model, DebugModelInfo* modelInfo, MMemIO* writer) {
    modelInfo->numVertices = (model->verticesDataSize / sizeof(VertexData)) >> 1;

    MStringAppendf(writer, "vertices: ; %d\n", (int)modelInfo->numVertices);

    u16* vertexData = (u16*) (((u8*)model) + model->vertexDataOffset);

    int buffSize = 256;
    char buff1[buffSize];

    for (int i = 0; i < modelInfo->numVertices; i++) {
        int vi = i << 1;

        // 1 byte type, 3 bytes for (x, y, z)
        u16 vpacked1 = vertexData[vi];     // bx
        u16 vpacked2 = vertexData[vi + 1]; // dx

        u8 vertexType = vpacked1 >> 8;

        i16 vdata1 = lo8s(vpacked1);
        i16 vdata2 = hi8s(vpacked2);
        i16 vdata3 = lo8s(vpacked2);

        i32 preSize = writer->size;
        MStringAppend(writer, "  ");
        switch (vertexType) {
            case 0x3:
            case 0x4:
                if (vdata2 < 0 || vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "savg %d, %d", (int)vdata2, (int)vdata3);
                break;
            case 0x5:
            case 0x6:
                if (vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "neg %d", (int)vdata3);
                break;
            case 0x7:
            case 0x8:
                if (vdata2 < 0 || vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "rand %d, %d, %d", (int)vdata3, (int)vdata2, (int)vdata1);
                break;
            case 0x9:
            case 0xa:
                MStringAppendf(writer, "%2x %d, %d, %d", (int)vertexType, (int)vdata1, (int)vdata2, (int)vdata3);
                break;
            case 0xb:
            case 0xc:
                if (vdata2 < 0 || vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "avg %d, %d", (int)vdata2, (int)vdata3);
                break;
            case 0xd:
            case 0xe:
                if (vdata2 < 0 || vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "savg2 %d, %d", (int)vdata2, (int)vdata3);
                break;
            case 0x0f:
            case 0x10:
                if (vdata2 < 0 || vdata3 < 0 || vdata1 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "addsub %d + %d - %d", (int)vdata3, (int)vdata2, (int)vdata1);
                break;
            case 0x11:
            case 0x12:
                if (vdata2 < 0 || vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "add %d, %d", (int)vdata2, (int)vdata3);
                break;
            case 0x13:
            case 0x14: {
                PrintParam8Base10(buff1, buffSize, vdata1 & 0xff);
                if (vdata2 < 0 || vdata3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                MStringAppendf(writer, "lerp %d, %d, %s", (int)vdata2, (int)vdata3, buff1);
                break;
            }
            default:
                MStringAppendf(writer, "%d, %d, %d", (int)vdata1, (int)vdata2, (int)vdata3);
                break;
        }

        i32 lenWrote = (i32)writer->size - preSize;
        while (lenWrote < 30) {
            MStringAppend(writer, " ");
            lenWrote++;
        }

        MStringAppendf(writer, "; %3d : %04x:%04x\n", vi, (int)vpacked1, (int)vpacked2);
    }

    MStringAppend(writer, "\n");

    if (model->normalDataSize > 0) {
        modelInfo->numNormals = (model->normalDataSize / 4) - 1;
        MStringAppendf(writer, "normals: ; %d\n", (int)modelInfo->numNormals);

        u16* normalDataPtr = (u16*) (((u8*) model) + model->normalsOffset);
        for (int i = 0; i < modelInfo->numNormals; i++) {
            u16 n1 = normalDataPtr[(i << 1)];
            u16 n2 = normalDataPtr[(i << 1) + 1];

            i8 x = (i8) (n1 & 0xff);
            i8 y = (i8) (n2 >> 8);
            i8 z = (i8) (n2 & 0xff);

            i8 vi = (i8) (n1 >> 8);
            if (vi < 0) {
                modelInfo->referencesParent = TRUE;
            }

            u8* preSize = writer->pos;

            MStringAppendf(writer, "  %3d (%d, %d, %d)", (int)vi, (int)x, (int)y, (int)z);

            int lenWrote = writer->pos - preSize;
            while (lenWrote < 30) {
                MStringAppend(writer, " ");
                lenWrote++;
            }
            MStringAppendf(writer, "; %3d %04x:%04x\n", (int)((1 + i) << 1), (int)n1, (int)n2);
        }

        MStringAppend(writer, "\n");
    }
}

static void DumpComplexInfo(MMemIO* dataReader, u64 offsetBegin, ByteCodeTraceArray* byteCodeTrace, u32 onlyLabels, MMemIO* writer) {
    u8* origPos = dataReader->pos;
    while (!MMemReadDone(dataReader)) {
        int insOffset = offsetBegin + (dataReader->pos - origPos);
        u16 data0 = 0;
        MMemReadU16(dataReader, &data0);

        if (onlyLabels) {
            MStringAppend(writer, "  ");
        } else {
            ByteCodeTrace* trace = NULL;
            if (byteCodeTrace) {
                trace = TraceForOffset(byteCodeTrace, insOffset);
            }
            if (trace) {
                MStringAppendf(writer, "*%05x: ", insOffset);
            } else {
                MStringAppendf(writer, " %05x: ", insOffset);
            }
        }

        u8 func = ((data0 >> 9) & 0x7);
        switch (func) {
            case RComplexFunc_Done:
                MStringAppend(writer, "cdone");
                return;
            case RComplexFunc_Bezier: {
                u16 data1 = 0;
                MMemReadU16(dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(dataReader, &data2);
                u16 vertexIndex1 = (data1 >> 8);
                u16 vertexIndex2 = (data1 & 0xff);
                u16 vertexIndex3 = (data2 >> 8);
                u16 vertexIndex4 = (data2 & 0xff);
                MStringAppendf(writer, "cbezier %d, %d, %d, %d",
                               (int)vertexIndex1, (int)vertexIndex2, (int)vertexIndex3, (int)vertexIndex4);
                break;
            }
            case RComplexFunc_Line: {
                u16 data1 = 0;
                MMemReadU16(dataReader, &data1);
                u16 vertexIndex1 = (data0 & 0xff);
                u16 vertexIndex2 = (data1 & 0xff);
                MStringAppendf(writer, "cline %d, %d", (int)vertexIndex1, (int)vertexIndex2);
                break;
            }
            case RComplexFunc_LineCont: {
                u16 vertexIndex1 = (data0 & 0xff);
                MStringAppendf(writer, "clinec %d", (int)vertexIndex1);
                break;
            }
            case RComplexFunc_BezierCont: {
                u16 data1 = 0;
                MMemReadU16(dataReader, &data1);
                u16 vertexIndex2 = (data0 & 0xff);
                u16 vertexIndex3 = (data1 >> 8);
                u16 vertexIndex4 = (data1 & 0xff);
                MStringAppendf(writer, "cbezierc %d, %d, %d",
                        (int)vertexIndex2, (int)vertexIndex3, (int)vertexIndex4);
                break;
            }
            case RComplexFunc_LineJoin: {
                MStringAppendf(writer, "cjoin");
                break;
            }
            case RComplexFunc_Circle: {
                u16 data1 = 0;
                MMemReadU16(dataReader, &data1);
                u16 vertexIndex1 = (data0 & 0xff);
                u16 scale = (data1 >> 8);
                u16 normalIndex1 = (data1 & 0x7f);
                MStringAppendf(writer, "coval normal:%d scale:%d %d", (int)normalIndex1,
                        (int)scale, (int)vertexIndex1);
                break;
            }
            default:
                MStringAppendf(writer, "cunknown func:%d", func);
                break;
        }
        MStringAppend(writer, "\n");
    }
}

static void DumpSubModelSetup(DebugModelInfo* modelInfo, MMemIO* writer,
                              ModelIndicesArray* modelIndexes, MMemIO* dataReader,
                              u16 data0, u16 data0_12, u16 data1) {

    int buffSize = 256;
    char buff1[buffSize];

    i16 vertexIndex = lo8s(data1);
    if (vertexIndex < 0) {
        modelInfo->referencesParent = TRUE;
    }

    u16 modelIndex = (data0 >> 5) & 0x3ff;

    u16 transformLight = !(data1 & (u16) 0x4000);

    u16 axis = (data1 >> 8) & 0x7;
    char axisFlips[3];
    b32 doFlip = TRUE;
    switch (axis) {
        case 0:
            axisFlips[0] = 'x';
            axisFlips[1] = 'y';
            axisFlips[2] = 'z';
            break;
        case 1:
            axisFlips[0] = 'y';
            axisFlips[1] = 'z';
            axisFlips[2] = 'x';
            break;
        case 2:
            axisFlips[0] = 'z';
            axisFlips[1] = 'x';
            axisFlips[2] = 'y';
            break;
        case 3:
            axisFlips[0] = 'z';
            axisFlips[1] = 'y';
            axisFlips[2] = 'x';
            break;
        case 4:
            axisFlips[0] = 'y';
            axisFlips[1] = 'x';
            axisFlips[2] = 'z';
            break;
        case 5:
            axisFlips[0] = 'x';
            axisFlips[1] = 'z';
            axisFlips[2] = 'y';
            break;
        case 6:
            doFlip = FALSE;
            break;
        case 7:
            axisFlips[0] = '?';
            axisFlips[1] = '?';
            axisFlips[2] = '?';
            break;
        default:
            break;
    }

    int pos = 0;
    if (data1 & 0x2000) {
        buff1[pos++] = '-';
        doFlip = TRUE;
    }
    buff1[pos++] = axisFlips[0];
    if (data1 & 0x1000) {
        buff1[pos++] = '-';
        doFlip = TRUE;
    }
    buff1[pos++] = axisFlips[1];
    if (data1 & 0x0800) {
        buff1[pos++] = '-';
        doFlip = TRUE;
    }
    buff1[pos++] = axisFlips[2];
    buff1[pos] = 0;

    MArrayAdd(*modelIndexes, modelIndex);

    MStringAppendf(writer, "model index:%d v:%d", (int)modelIndex, (int)vertexIndex);

    if (transformLight) {
        MStringAppendf(writer, " light");
    }

    if (doFlip) {
        MStringAppendf(writer, " flip:%s", buff1);
    }

    if (data1 & (u16) 0x8000) {
        u16 data2 = 0;
        MMemReadU16(dataReader, &data2);
        u16 data3 = 0;
        MMemReadU16(dataReader, &data3);
        i16 v1i = lo8s(data3);
        i16 v2i = lo8s(data2);
        i16 v3i = hi8s(data2);
        i16 v4i = hi8s(data3);

        MStringAppendf(writer, " frame:[%d", (int)v1i);
        if (v2i != 127) {
            MStringAppendf(writer, ", %d", (int)v2i);
            if (v3i != 127) {
                MStringAppendf(writer, ", %d", (int)v3i);
                if (v4i != 127) {
                    MStringAppendf(writer, ", %d", (int)v4i);
                }
            }
        }
        MStringAppend(writer, "]");
    }
}

static void DumpModelCode(const u8* codeStart, const DebugModelParams* debugModelParams, DebugModelInfo* modelInfo,
        MMemIO* strOutput) {

    int buffSize = 256;
    char buff1[buffSize];
    char buff2[buffSize];
    char buff3[buffSize];
    b32 lastWasDone = FALSE;
    u64 offsetBegin = debugModelParams->offsetBegin;
    u64 largestOffset = offsetBegin;

    u64Array codeOffsets;
    MArrayInit(codeOffsets);

    MMemIO dataReader;
    MMemReadInit(&dataReader, (u8*) codeStart, debugModelParams->maxSize);

    while (!MMemReadDone(&dataReader)) {
        u8* begin = dataReader.pos;
        u64 insOffset = offsetBegin + (dataReader.pos - codeStart);
        if (lastWasDone && largestOffset < insOffset) {
            break;
        } else {
            lastWasDone = FALSE;
        }

        u16 data0 = 0;
        MMemReadU16(&dataReader, &data0);
        u16 func = (data0 & (u16)0x1f);
        u16 data0_12 = data0 >> 4;

        ByteCodeTrace* trace = NULL;
        if (debugModelParams->byteCodeTrace) {
            trace = TraceForOffset(debugModelParams->byteCodeTrace, insOffset);
        }

        if (debugModelParams->onlyLabels) {
            char label[20];
            snprintf(label, 20, "%05llx", insOffset);
            b32 hasLabel = FALSE;
            for (int i = 0; i < MArraySize(codeOffsets); i++) {
                u64 codeOffset = MArrayGet(codeOffsets, i);
                if (codeOffset == insOffset) {
                    MStringAppendf(strOutput, "%05llx: ", insOffset);
                    hasLabel = TRUE;
                    break;
                }
            }
            if (!hasLabel) {
                MStringAppend(strOutput, "  ");
            }
        } else {
            if (trace) {
                MStringAppendf(strOutput, "*%05lx: ", insOffset);
            } else {
                MStringAppendf(strOutput, " %05lx: ", insOffset);
            }
        }

        switch (func) {
            case Render_DONE:
                MStringAppend(strOutput, "done");
                lastWasDone = TRUE;
                break;
            case Render_CIRCLE: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 type = (data2 & 0xff);
                i8 vertexIndex = (i8)(data2 >> 8);
                PrintParam16Base10(buff1, buffSize, data1);
                if (type & 0x80) {
                    MStringAppendf(strOutput, "highlight vertex:%d radius:%s colour:#%03x", (int)vertexIndex,
                            buff1, colour);
                } else {
                    MStringAppendf(strOutput, "circle vertex:%d radius:%s colour:#%03x", (int)vertexIndex,
                            buff1, colour);
                }

                if (vertexIndex < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_LINE: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                i16 vertexIndex1 = hi8s(data1);
                i16 vertexIndex2 = lo8s(data1);
                MStringAppendf(strOutput, "line %d, %d colour:#%03x", (int)vertexIndex1, (int)vertexIndex2,
                        (int)colour);
                if (vertexIndex1 < 0 || vertexIndex2 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_TRI: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, & data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                i16 vertexIndex1 = hi8s(data1);
                i16 vertexIndex2 = lo8s(data1);
                i16 vertexIndex3 = hi8s(data2);
                u16 normalIndex = (data2 & 0x7f);
                MStringAppendf(strOutput, "tri %d, %d, %d colour:#%03x", (int)vertexIndex1, (int)vertexIndex2,
                               (int)vertexIndex3, (int)colour);

                if (normalIndex) {
                    MStringAppendf(strOutput, " normal:%d", (int)normalIndex);
                }

                if (vertexIndex1 < 0 || vertexIndex2 < 0 || vertexIndex3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }

                break;
            }
            case Render_QUAD: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 data3 = 0;
                MMemReadU16(&dataReader, &data3);
                i16 vertexIndex1 = hi8s(data1);
                i16 vertexIndex2 = lo8s(data1);
                i16 vertexIndex3 = hi8s(data2);
                i16 vertexIndex4 = lo8s(data2);
                u16 normalIndex = (data3 & 0x7f);
                MStringAppendf(strOutput, "quad %d, %d, %d, %d colour:#%03x",
                               (int)vertexIndex1, (int)vertexIndex2, (int)vertexIndex3, (int)vertexIndex4, (int)colour);

                if (normalIndex) {
                    MStringAppendf(strOutput, " normal:%d", (int)normalIndex);
                }

                if (vertexIndex1 < 0 || vertexIndex2 < 0 || vertexIndex3 < 0 || vertexIndex4 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_COMPLEX: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 normalIndex = (data1 & 0x7f);
                u16 skipBytes = (data1 >> 8) + 2;
                u64 jumpOffset = insOffset + skipBytes + 2;

                MStringAppend(strOutput, "complex ");

                if (normalIndex) {
                    MStringAppendf(strOutput, "normal:%d ", (int)normalIndex);
                }

                MStringAppendf(strOutput, "colour:#%03x loc:%05x\n", (int)colour, (int)jumpOffset);

                MArrayAdd(codeOffsets, jumpOffset);

                if (jumpOffset > largestOffset) {
                    largestOffset = jumpOffset;
                }
                DumpComplexInfo(&dataReader, insOffset + 4, debugModelParams->byteCodeTrace,
                        debugModelParams->onlyLabels, strOutput);
                break;
            }
            case Render_BATCH: {
                u16 batchControl = data0_12 >> 1;
                if (batchControl == 0) {
                    MStringAppendf(strOutput, "batch end");
                } else if (batchControl == 0x7ff) {
                    MStringAppendf(strOutput, "batch begin z:obj");
                } else if (batchControl == 0x7fe) {
                    MStringAppendf(strOutput, "batch begin z:inf");
                } else if (batchControl == 0x7fd) {
                    i16 z = 0;
                    MMemReadI16(&dataReader, &z);
                    MStringAppendf(strOutput, "batch begin z:%d", (int)z);
                } else {
                    i16 vertexIndex = lo8s(batchControl);
                    if (vertexIndex < 0) {
                        modelInfo->referencesParent = TRUE;
                    }
                    u8 upperControl = (batchControl >> 8);
                    u16 data1;
                    switch (upperControl) {
                        case 0x1: {
                            MStringAppendf(strOutput, "batch begin z:max vertex:[%d", (int)vertexIndex);
                            do {
                                MMemReadU16(&dataReader, &data1);
                                MStringAppendf(strOutput, ", %d", (int)(data1 & 0xff));
                            } while (data1 & 0x8000);
                            MStringAppendf(strOutput, "]");
                            break;
                        }
                        case 0x3: {
                            MStringAppendf(strOutput, "batch begin z:min vertex:[%d", vertexIndex);
                            do {
                                MMemReadU16(&dataReader, &data1);
                                MStringAppendf(strOutput, ", %d", (int)(data1 & 0xff));
                            } while (data1 & 0x8000);
                            MStringAppendf(strOutput, "]");
                            break;
                        }
                        case 0x7: {
                            MMemReadU16(&dataReader, &data1);
                            MStringAppendf(strOutput, "batch begin z:%d vertex:%d", (int)data1, (int)vertexIndex);
                            break;
                        }
                        default:
                            MStringAppendf(strOutput, "batch begin vertex:%d", (int)vertexIndex);
                            break;
                    }
                }
                break;
            }
            case Render_MIRRORED_TRI: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                i16 vertexIndex1 = hi8s(data1);
                i16 vertexIndex2 = lo8s(data1);
                i16 vertexIndex3 = hi8s(data2);
                u16 normalIndex = (data2 & 0x7f);

                MStringAppendf(strOutput, "mtri %d, %d, %d colour:#%03x",
                               (int)vertexIndex1, (int)vertexIndex2, (int)vertexIndex3, (int)colour);

                if (normalIndex) {
                    MStringAppendf(strOutput, " normal:%d", (int)normalIndex);
                }

                if (vertexIndex1 < 0 || vertexIndex2 < 0 || vertexIndex3 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_MIRRORED_QUAD: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 data3 = 0;
                MMemReadU16(&dataReader, &data3);
                i16 vertexIndex1 = hi8s(data1);
                i16 vertexIndex2 = lo8s(data1);
                i16 vertexIndex3 = hi8s(data2);
                i16 vertexIndex4 = lo8s(data2);
                u16 normalIndex = (data3 & 0x7f);

                MStringAppendf(strOutput, "mquad %d, %d, %d, %d colour:#%03x",
                               (int)vertexIndex1, (int)vertexIndex2, (int)vertexIndex3, (int)vertexIndex4, (int)colour);

                if (normalIndex) {
                    MStringAppendf(strOutput, " normal:%d", (int)normalIndex);
                }

                if (vertexIndex1 < 0 || vertexIndex2 < 0 || vertexIndex3 < 0 || vertexIndex4 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_TEARDROP: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                i8 vertexIndex1 = (i8)lo8s(data1);
                i8 vertexIndex2 = (i8)hi8s(data1);
                MStringAppendf(strOutput, "teardrop radius:%d colour:#%03x %d, %d", (int)data2, (int)colour,
                               (int)vertexIndex1, (int)vertexIndex2);
                if (vertexIndex1 < 0 || vertexIndex2 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_VTEXT: {
                u16 colour = data0_12 & 0xfff;
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 data3 = 0;
                MMemReadU16(&dataReader, &data3);
                i16 vertexIndex1 = lo8s(data2);
                u16 transform = (data2 >> 8);
                u16 scale = (data1 >> 8) & 0xf;
                u16 fontIndex = (data1 >> 12) & 0xf;
                u16 normalIndex = (data1 & 0x7f);
                u16 stringIndex = data3 & 0x7f;
                u16 stringType = data3;

                char* stringIndexType;
                if (stringType & 0x8000) {
                    stringIndexType = "??1";
                } else if (stringType & 0x4000) {
                    stringIndexType = "module";
                } else {
                    stringIndexType = "model";
                }

                MArrayAdd(modelInfo->modelIndexes, fontIndex);

                MStringAppendf(strOutput, "vtext vertex:%d transform:%d scale:%d font:%d string:%s[%d] colour:#%03x",
                               (int)vertexIndex1, (int)transform, (int)scale, (int)fontIndex, stringIndexType,
                               (int)stringIndex, (int)colour);

                if (normalIndex) {
                    MStringAppendf(strOutput, " normal:%d", (int)normalIndex);
                }

                if (vertexIndex1 < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                break;
            }
            case Render_IF: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 skipBytes = data0_12 & 0xfffe;
                if (skipBytes > 0) {
                    u32 jumpOffset = insOffset + skipBytes + 4;
                    MArrayAdd(codeOffsets, jumpOffset);
                    if (data1 & 0x8000) {
                        u16 normalIndex = (data1 & 0x7f);
                        MStringAppendf(strOutput, "if > normal:%d loc:%05x", (int)normalIndex, (int)jumpOffset);
                    } else {
                        u16 z = data1 >> 1;
                        MStringAppendf(strOutput, "if > z:%d loc:%05x", (int)z, jumpOffset);
                    }
                    if (jumpOffset > largestOffset) {
                        largestOffset = jumpOffset;
                    }
                } else {
                    if (data1 & 0x8000) {
                        u16 normalIndex = (data1 & 0x7f);
                        MStringAppendf(strOutput, "if > normal:%d end", (int)normalIndex);
                    } else {
                        u16 z = data1 >> 1;
                        MStringAppendf(strOutput, "if > z:%d end", (int)z);
                    }
                }
                break;
            }
            case Render_IF_NOT: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 skipBytes = data0_12 & 0xfffe;
                if (skipBytes > 0) {
                    u32 jumpOffset = insOffset + skipBytes + 4;
                    MArrayAdd(codeOffsets, jumpOffset);
                    if (data1 & 0x8000) {
                        u16 normalIndex = (data1 & 0x7f);
                        MStringAppendf(strOutput, "if < normal:%d loc:%05x", (int)normalIndex, jumpOffset);
                    } else {
                        u16 z = data1 >> 1;
                        MStringAppendf(strOutput, "if < z:%d loc:%05x", (int)z, jumpOffset);
                    }
                    if (jumpOffset > largestOffset) {
                        largestOffset = jumpOffset;
                    }
                } else {
                    if (data1 & 0x8000) {
                        u16 normalIndex = (data1 & 0x7f);
                        MStringAppendf(strOutput, "if < normal:%d", (int)normalIndex);
                    } else {
                        u16 z = data1 >> 1;
                        MStringAppendf(strOutput, "if < z:%d", (int)z);
                    }
                }
                break;
            }
            case Render_CALC_A:
            case Render_CALC_B: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                PrintParam8Base10(buff1, buffSize, data1 & 0xff);
                PrintParam8Base10(buff2, buffSize, data1 >> 8);
                u16 mathFunc = (data0 >> 4) & 0xf;
                u16 resultOffset = (data0 >> 8) & 0x7;

                switch (mathFunc) {
                    case MathFunc_Add:
                        snprintf(buff3, buffSize, "%s + %s", buff1, buff2);
                        break;
                    case MathFunc_Sub:
                        snprintf(buff3, buffSize, "%s - %s", buff1, buff2);
                        break;
                    case MathFunc_Mult:
                        snprintf(buff3, buffSize, "%s * %s", buff1, buff2);
                        break;
                    case MathFunc_Div:
                        snprintf(buff3, buffSize, "%s / %s", buff1, buff2);
                        break;
                    case MathFunc_DivPower2:
                        snprintf(buff3, buffSize, "%s >> %s", buff1, buff2);
                        break;
                    case MathFunc_MultPower2:
                        snprintf(buff3, buffSize, "%s << %s", buff1, buff2);
                        break;
                    case MathFunc_Max:
                        snprintf(buff3, buffSize, "max(%s, %s)", buff1, buff2);
                        break;
                    case MathFunc_Min:
                        snprintf(buff3, buffSize, "min(%s, %s)", buff1, buff2);
                        break;
                    case MathFunc_Mult2:
                        snprintf(buff3, buffSize, "%s * %s", buff1, buff2);
                        break;
                    case MathFunc_DivPower2Signed:
                        snprintf(buff3, buffSize, "%s / (1 ^ %s)", buff1, buff2);
                        break;
                    case MathFunc_RGetIndirectOffset:
                        snprintf(buff3, buffSize, "indirect(%s, %s)", buff1, buff2);
                        break;
                    case MathFunc_ZeroIfGreater:
                        snprintf(buff3, buffSize, "%s > %s", buff1, buff2);
                        break;
                    case MathFunc_ZeroIfLess:
                        snprintf(buff3, buffSize, "%s < %s", buff1, buff2);
                        break;
                    case MathFunc_MultSine: {
                        PrintParam8HexFract(buff1, buffSize, data1 & 0xff);
                        snprintf(buff3, buffSize, "%s * sine(%s)", buff1, buff2);
                        break;
                    }
                    case MathFunc_MultCos: {
                        PrintParam8HexFract(buff1, buffSize, data1 & 0xff);
                        snprintf(buff3, buffSize, "%s * cos(%s)", buff1, buff2);
                        break;
                    }
                    case MathFunc_And:
                        snprintf(buff3, buffSize, "%s & %s", buff1, buff2);
                        break;
                }

                MStringAppendf(strOutput, "tmp[%d] = %s", resultOffset, buff3);

                if (trace) {
                    MStringAppendf(strOutput, " ; %d", trace->result);
                }

                break;
            }
            case Render_MODEL: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                DumpSubModelSetup(modelInfo, strOutput, &(modelInfo->modelIndexes), &dataReader, data0, data0_12, data1);
                break;
            }
            case Render_AUDIO_CUE: {
                MStringAppendf(strOutput, "sound %d", (int)data0_12);
                break;
            }
            case Render_CONE: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 data3 = 0;
                MMemReadU16(&dataReader, &data3);

                i16 v1 = lo8s(data1);
                i16 v2 = hi8s(data1);
                if (v1 < 0 || v2 < 0) {
                    modelInfo->referencesParent = TRUE;
                }

                u16 r1 = (data2 >> 8);
                u16 r2 = (data3 >> 8);

                u16 n1 = (data2 & 0x7f);
                u16 n2 = (data3 & 0x7f);

                const char* cap1Render = (data2 & 0x80) ? " c:1" : "";
                const char* cap2Render = (data3 & 0x80) ? " c:1" : "";

                MStringAppendf(strOutput, "cone colour:#%03x (v:%d n:%d r:%d%s) (v:%d n:%d r:%d%s)",
                               (int)data0_12, (int)v1, (int)n1, (int)r1, cap1Render,
                               (int)v2, (int)n2, (int)r2, cap2Render);
                break;
            }
            case Render_CONE_COLOUR_CAP: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 data3 = 0;
                MMemReadU16(&dataReader, &data3);

                i16 v1 = lo8s(data1);
                i16 v2 = hi8s(data1);
                if (v1 < 0 || v2 < 0) {
                    modelInfo->referencesParent = TRUE;
                }

                u16 r1 = (data2 >> 8);
                u16 r2 = (data3 >> 8);

                u16 n1 = (data2 & 0x7f);
                u16 n2 = (data3 & 0x7f);

                u16 colour1 = 0;
                MMemReadU16(&dataReader, &colour1);
                u16 colour2 = 0;
                MMemReadU16(&dataReader, &colour2);

                MStringAppendf(strOutput, "cone colour:#%03x (v:%d n:%d r:%d", (int)data0_12, (int)v1, (int)n1,
                        (int)r1);
                if (data2 & 0x80) {
                    MStringAppendf(strOutput, " c:#%03x", (int)colour1);
                }

                MStringAppendf(strOutput, ") (v:%d n:%d r:%d", (int)v2, (int)n2, (int)r2);
                if (data3 & 0x80) {
                    MStringAppendf(strOutput, " c:#%03x", (int)colour2);
                }

                MStringAppend(strOutput, ")");
                break;
            }
            case Render_BITMAP_TEXT: {
                // Bitmap text
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                MStringAppendf(strOutput, "btext d1:%d d2:%d", (int)data0_12, (int)data1);
                break;
            }
            case Render_IF_NOT_VAR: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);

                PrintParam8Base10(buff1, buffSize, data1 & 0xff);
                u16 skipBytes = data0_12 & 0xfffe;
                u16 bit = data1 >> 8;

                if (skipBytes) {
                    u32 jumpOffset = insOffset + skipBytes + 4;
                    MArrayAdd(codeOffsets, jumpOffset);

                    if (bit) {
                        MStringAppendf(strOutput, "if !bit(%s, %d) loc:%05x", buff1, (int)bit, jumpOffset);
                    } else {
                        MStringAppendf(strOutput, "if !%s loc:%05x", buff1, jumpOffset);
                    }
                    if (jumpOffset > largestOffset) {
                        largestOffset = jumpOffset;
                    }
                } else {
                    if (bit) {
                        MStringAppendf(strOutput, "if !bit(%s, %d) end", buff1, (int)bit);
                    } else {
                        MStringAppendf(strOutput, "if !%s end", buff1);
                    }
                }

                break;
            }
            case Render_IF_VAR: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);

                PrintParam8Base10(buff1, buffSize, data1 & 0xff);
                u16 skipBytes = data0_12 & 0xfffe;
                u16 bit = data1 >> 8;

                if (skipBytes) {
                    u32 jumpOffset = insOffset + skipBytes + 4;
                    MArrayAdd(codeOffsets, jumpOffset);
                    if (bit) {
                        MStringAppendf(strOutput, "if bit(%s, %d) loc:%05x", buff1, (int)bit, jumpOffset);
                    } else {
                        MStringAppendf(strOutput, "if %s loc:%05x", buff1, jumpOffset);
                    }
                    if (jumpOffset > largestOffset) {
                        largestOffset = jumpOffset;
                    }
                } else {
                    if (bit) {
                        MStringAppendf(strOutput, "if bit(%s, %d) end", buff1, (int)bit);
                    } else {
                        MStringAppendf(strOutput, "if %s end", buff1);
                    }
                }
                break;
            }
            case Render_CLEARZ: {
                if (data0 & 0x8000) {
                    MStringAppendf(strOutput, "ztree pop");
                } else {
                    i16 vi = lo8s(data0_12 >> 1);
                    MStringAppendf(strOutput, "ztree push v:%d", (int)vi);
                }
                break;
            }
            case Render_LINE_BEZIER: {
                // Line Bezier
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);
                u16 data3 = 0;
                MMemReadU16(&dataReader, &data3);

                u16 normalIndex = data3 & 0x7f;

                i16 v1i = hi8s(data1);
                i16 v2i = lo8s(data1);
                i16 v3i = hi8s(data2);
                i16 v4i = lo8s(data2);
                if (v1i < 0 || v2i < 0 || v3i < 0 || v4i < 0) {
                    modelInfo->referencesParent = TRUE;
                }

                u16 colourParam = (data0_12 & 0xfff);

                MStringAppendf(strOutput, "bline %d, %d, %d, %d normal:%d colour:#%03x",
                               (int)v1i, (int)v2i, (int)v3i, (int)v4i, (int)normalIndex, (int)colourParam);
                break;
            }
            case Render_IF_VIEWSPACE_DIST: {
                // Skip if two vertices are within / past a certain distance in view coordinates
                // Test skipped if any vertex is behind camera
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);

                i16 v1i = lo8s(data2);
                i16 v2i = hi8s(data2);
                if (v1i < 0 || v2i < 0) {
                    modelInfo->referencesParent = TRUE;
                }
                u16 skipBytes = data0_12 & 0xfffe;
                u32 jumpOffset = insOffset + skipBytes + 6;
                if (skipBytes) {
                    MArrayAdd(codeOffsets, jumpOffset);
                }

                if (data1 & 0x8000) {
                    i32 min = data1 ^ 0x8000;
                    if (skipBytes) {
                        MStringAppendf(strOutput, "if dist(%d, %d) < %d loc:%05x", (int)v1i, (int)v2i, (int)min,
                                jumpOffset);
                        if (jumpOffset > largestOffset) {
                            largestOffset = jumpOffset;
                        }
                    } else {
                        MStringAppendf(strOutput, "if dist(%d, %d) < %d end", (int)v1i, (int)v2i, (int)min);
                    }
                } else {
                    i32 max = data1;
                    if (skipBytes) {
                        MStringAppendf(strOutput, "if dist(%d, %d) > %d loc:%05x", (int)v1i, (int)v2i, (int)max,
                                jumpOffset);
                        if (jumpOffset > largestOffset) {
                            largestOffset = jumpOffset;
                        }
                    } else {
                        MStringAppendf(strOutput, "if dist(%d, %d) > %d end", (int)v1i, (int)v2i, (int)max);
                    }
                }
                break;
            }
            case Render_BALLS: {
                u16 colour = data0_12 & 0xfff;
                MStringAppendf(strOutput, "balls colour:#%03x ", (int)colour);

                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                PrintBallSize(buff1, buffSize, data1);
                MStringAppendf(strOutput, "%s ", buff1);

                b32 start = TRUE;
                while (!MMemReadDone(&dataReader)) {
                    u16 data2 = 0;
                    MMemReadU16(&dataReader, &data2);
                    i16 v1 = hi8s(data2);
                    i16 v2 = lo8s(data2);
                    if (v1 < 0 || v2 < 0) {
                        modelInfo->referencesParent = TRUE;
                    }
                    if (v1 == 0x7f) {
                        break;
                    }
                    if (!start) {
                        MStringAppend(strOutput, ", ");
                    }
                    if (v2 == 0x7f) {
                        MStringAppendf(strOutput, "%d", (int)v1);
                        break;
                    }
                    MStringAppendf(strOutput, "%d, %d", (int)v1, (int)v2);
                    start = FALSE;
                }

                break;
            }
            case Render_MATRIX_SETUP: {
                if (data0_12 & 0x800) {
                    MStringAppendf(strOutput, "mident");
                } else {
                    u16 axis = (data0_12 >> 1) & 0x3;

                    const char *axisStr = 0;
                    switch (axis) {
                        case 0:
                            axisStr = "x";
                            break;
                        case 1:
                            axisStr = "y";
                            break;
                        case 2:
                            axisStr = "z";
                            break;
                        case 4:
                            axisStr = "invalid";
                            break;
                    }
                    MStringAppendf(strOutput, "mlight axis:%s", axisStr);
                }
                break;
            }
            case Render_COLOUR: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 coloursProvided = data1 >> 8;
                u16 normalIndex = data1 & 0x7f;
                u16 colourParam = data0_12 & 0xffe;
                if (coloursProvided) {
                    PrintParam8Base10(buff1, buffSize, coloursProvided);
                    u16 extraColours[7];
                    for (int i = 0; i < 7; i++) {
                        MMemReadU16(&dataReader, extraColours + i);
                        extraColours[i] &= 0xfff;
                    }
                    MStringAppendf(strOutput, "colour normal:%d index:%s colours:[#%03x, #%03x, #%3x, #%03x, #%03x, #%03x, #%03x, #%03x]",
                                   (int)normalIndex, buff1, (int)colourParam,
                                   (int)extraColours[0], (int)extraColours[1], (int)extraColours[2], (int)extraColours[3],
                                   (int)extraColours[4], (int)extraColours[5], (int)extraColours[6]);
                } else {
                    MStringAppendf(strOutput, "colour normal:%d colours:[#%03x]",
                            (int)normalIndex, (int)colourParam);
                }
                break;
            }
            case Render_MODEL_SCALE: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);

                DumpSubModelSetup(modelInfo, strOutput, &modelInfo->modelIndexes, &dataReader, data0, data0_12, data1);

                u16 colourParam = (data2 << 1) & 0xfff;

                if (data2 & 0x800) {
                    u16 ix = data2 >> 0xc;
                    MStringAppendf(strOutput, " scale:tmp[%d]", (int)ix);
                } else {
                    u16 scale = data2 >> 0xc;
                    MStringAppendf(strOutput, " scale:%d", (int)scale);
                }

                MStringAppendf(strOutput, " colour:#%03x", (int)colourParam);

                break;
            }
            case Render_MATRIX_TRANSFORM: {
                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);

                b32 doFlip = FALSE;

                if (!(data0 & 0x8000)) {
                    doFlip = TRUE;

                    u16 flip = (data0_12 >> 3) & 0xff;
                    u16 axis = flip & 0x7;
                    char axisFlips[3];
                    switch (axis) {
                        case 1:
                            axisFlips[0] = 'y';
                            axisFlips[1] = 'z';
                            axisFlips[2] = 'x';
                            break;
                        case 2:
                            axisFlips[0] = 'z';
                            axisFlips[1] = 'x';
                            axisFlips[2] = 'y';
                            break;
                        case 3:
                            axisFlips[0] = 'z';
                            axisFlips[1] = 'y';
                            axisFlips[2] = 'x';
                            break;
                        case 4:
                            axisFlips[0] = 'y';
                            axisFlips[1] = 'x';
                            axisFlips[2] = 'z';
                            break;
                        case 5:
                            axisFlips[0] = 'x';
                            axisFlips[1] = 'z';
                            axisFlips[2] = 'y';
                            break;
                        default:
                            axisFlips[0] = 'x';
                            axisFlips[1] = 'y';
                            axisFlips[2] = 'z';
                            break;
                    }
                    int pos = 0;
                    if (flip & 0x20) {
                        buff1[pos++] = '-';
                    }
                    buff1[pos++] = axisFlips[0];
                    if (flip & 0x10) {
                        buff1[pos++] = '-';
                    }
                    buff1[pos++] = axisFlips[1];
                    if (flip & 0x08) {
                        buff1[pos++] = '-';
                    }
                    buff1[pos++] = axisFlips[2];
                    buff1[pos] = 0;
                }

                u16 axis = (data0_12 >> 1) & 0x3;

                const char* axisStr = 0;
                switch (axis) {
                    case 0:
                        axisStr = "x";
                        break;
                    case 1:
                        axisStr = "y";
                        break;
                    case 2:
                        axisStr = "z";
                        break;
                    case 3:
                    default:
                        axisStr = "???";
                        break;
                }

                PrintParam16Base10(buff2, buffSize, data1);

                if (doFlip) {
                    MStringAppendf(strOutput, "mrotate flip:%s axis:%s angle:%s", buff1, axisStr, buff2);
                } else {
                    MStringAppendf(strOutput, "mrotate axis:%s angle:%s", axisStr, buff2);
                }
                break;
            }
            case Render_MATRIX_COPY: {
                if (data0_12 == 0xc01) {
                    MStringAppendf(strOutput, "mcopy");
                } else {
                    MStringAppendf(strOutput, "msetup %x", (int)data0_12);
                }
                break;
            }
            case Render_PLANET: {
                MStringAppend(strOutput, "planet");

                u16 data1 = 0;
                MMemReadU16(&dataReader, &data1);
                u16 data2 = 0;
                MMemReadU16(&dataReader, &data2);

                u8* pos = dataReader.pos;

                i16 vi = lo8s(data2);
                if (vi < 0) {
                    modelInfo->referencesParent = TRUE;
                }

                u16 dataSize = data0_12 & 0xffe;

                MStringAppendf(strOutput, " v:%d", (int)vi);
                MStringAppendf(strOutput, " size:%d", (int)data1);

                u16 colour[4] = { 0, 0, 0, 0 };
                MMemReadU16(&dataReader, colour + 0);
                MMemReadU16(&dataReader, colour + 1);
                MMemReadU16(&dataReader, colour + 2);
                MMemReadU16(&dataReader, colour + 3);

                u16 colParam = colour[0] >> 12;

                u16 shade = 0;
                if (colParam & 0x8) {
                    if (colParam & 0x4) {
                        if (colParam & 0x2) {
                            shade = 3;
                        } else {
                            shade = 2;
                        }
                    } else {
                        shade = 1;
                    }
                }

                MStringAppendf(strOutput, " shade:%d colours:[#%03x, #%03x, #%03x, #%03x", (int)shade,
                        (int)(colour[0] & 0xfff), (int)(colour[1] & 0xfff), (int)(colour[2] & 0xfff),
                        (int)(colour[3] & 0xfff));

                if (colParam & 0x1) {
                    u16 data3 = 0;
                    MMemReadU16(&dataReader, &data3);
                    PrintParam8Base10(buff1, buffSize, data3);
                    MStringAppendf(strOutput, "] paletteSelect:%s palette:[", buff1);

                    for (int k = 0; k < 7; k++) {
                        MMemReadU16(&dataReader, colour + 0);
                        MMemReadU16(&dataReader, colour + 1);
                        MMemReadU16(&dataReader, colour + 2);
                        MMemReadU16(&dataReader, colour + 3);
                        MStringAppendf(strOutput, "[#%03x, #%03x, #%03x, #%03x]",
                                       (int)(colour[0] & 0xfff),
                                       (int)colour[1] & 0xfff,
                                       (int)colour[2] & 0xfff,
                                       (int)colour[3] & 0xfff);
                        if (k != 6) {
                            MStringAppend(strOutput, ", ");
                        }
                    }
                } else if (!(colParam & 0x4)) {
                    MMemReadU16(&dataReader, colour + 0);
                    MMemReadU16(&dataReader, colour + 1);
                    MMemReadU16(&dataReader, colour + 2);
                    MMemReadU16(&dataReader, colour + 3);

                    MStringAppendf(strOutput, ", #%03x, #%03x, #%03x, #%03x", (int)colParam,
                                   (int)(colour[0] & 0xfff), (int)(colour[1] & 0xfff),
                                   (int)(colour[2] & 0xfff), (int)(colour[3] & 0xfff));
                }
                MStringAppend(strOutput, "]");

                u16 atmosColour = 0;
                MMemReadU16(&dataReader, &atmosColour);
                MStringAppendf(strOutput, " atmosColour:#%03x", atmosColour);

                if (atmosColour) {
                    u16 atmosBandWidth = 0;
                    MMemReadU16(&dataReader, &atmosBandWidth);
                    u16 bandColour = 0;
                    MMemReadU16(&dataReader, &bandColour);
                    char firstChar = (atmosBandWidth & 0x4000) ? '1' : '0';
                    int bandwidth = (0x3fff & atmosBandWidth) << 2;
                    MStringAppendf(strOutput, " atmosBands:[#%c.%04x #%03x", firstChar, (int)bandwidth,
                                   (int)bandColour);
                    do {
                        MMemReadU16(&dataReader, &atmosBandWidth);
                        if (atmosBandWidth) {
                            MMemReadU16(&dataReader, &bandColour);
                            firstChar = (atmosBandWidth & 0x4000) ? '1' : '0';
                            bandwidth = (0x3fff & atmosBandWidth) << 2;
                            MStringAppendf(strOutput, ", #%c.%04x #%03x", firstChar, (int)bandwidth, (int)bandColour);
                        }
                    } while (atmosBandWidth);
                    MStringAppend(strOutput, "]");
                }

                MStringAppend(strOutput, " features:[");

                u32 dataRemaining = dataSize - (dataReader.pos - pos);

                int i = 0;
                while (dataRemaining) {
                    i8 featureControl = 0;
                    MMemReadI8(&dataReader, &featureControl);
                    dataRemaining--;
                    if (featureControl == 0) {
                        break;
                    }

                    if (i == 1) {
                        MStringAppend(strOutput, ", ");
                    }

                    i8 coord[3] = { 0, 0, 0 };
                    if (featureControl < 0) {
                        // Render surface circle on sphere at given point
                        MMemReadI8(&dataReader, coord + 0);
                        MMemReadI8(&dataReader, coord + 1);
                        MMemReadI8(&dataReader, coord + 2);
                        // angle used to determine radius of circle
                        i8 angle = 0;
                        MMemReadI8(&dataReader, &angle);
                        dataRemaining -= 4;
                        MStringAppendf(strOutput, "[%d, %d, %d, %d, %d]", (int)featureControl, (int)coord[0],
                                       (int)coord[1], (int)coord[2], (int)angle);
                    } else {
                        // Render complex surface poly on sphere
                        i8 detailLevel = 0;
                        MMemReadI8(&dataReader, &detailLevel);

                        MStringAppendf(strOutput, "[%d, %d", (int)featureControl, (int)detailLevel);

                        MMemReadI8(&dataReader, coord);
                        dataRemaining--;
                        while (coord[0]) {
                            // Coordinates
                            MStringAppend(strOutput, ", ");
                            MMemReadI8(&dataReader, coord + 1);
                            MMemReadI8(&dataReader, coord + 2);
                            MStringAppendf(strOutput, "[%d, %d, %d]", (int)coord[0], (int)coord[1], (int)coord[2]);
                            MMemReadI8(&dataReader, coord);
                            dataRemaining -= 3;
                        }
                        MStringAppendf(strOutput, "]");
                    }

                    i = 1;
                }
                MStringAppend(strOutput, "]");
                dataReader.pos = pos + dataSize;
                break;
            }
            default:
                MStringAppendf(strOutput, "unknown %d!", (int)func);
                break;
        }

        MStringAppend(strOutput, " ; ");
        u16* end = (u16*)dataReader.pos;
        for (u16* pos = (u16*)begin; pos < end; pos++) {
            MStringAppendf(strOutput, "%04x", (int)*pos);
            if (pos < end - 1) {
                MStringAppend(strOutput, ":");
            }
        }

        MStringAppend(strOutput, "\n");
    }

    MArrayFree(codeOffsets);
}

void DecompileModel(ModelData *model, u32 modelIndex, DebugModelParams* debugModelParams, DebugModelInfo* modelInfo, MMemIO* strOutput) {
    modelInfo->referencesParent = FALSE;

    if (modelIndex) {
        MStringAppendf(strOutput, "model: %d\n", (int)modelIndex);
    } else {
        MStringAppend(strOutput, "model:\n");
    }

    MStringAppendf(strOutput, "  scale1: %d,\n  scale2: %d,\n  radius: %d,\n  colour: #%03x\n\n",
                   (int)model->scale1, (int)model->scale2, (int)model->radius, (int)model->colour);

    MArrayInit(modelInfo->modelIndexes);

    DumpVerticesAndNormals(model, modelInfo, strOutput);

    MStringAppend(strOutput, "code:\n");
    u8* codeStart = ((u8*)model) + model->codeOffset;
    DumpModelCode(codeStart, debugModelParams, modelInfo, strOutput);

    MStringAppend(strOutput, "\n");
}

void DecompileFontModel(FontModelData* model, u32 modelIndex, DebugModelParams* debugModelParams,
        DebugModelInfo* modelInfo, MMemIO* strOutput) {

    modelInfo->referencesParent = FALSE;

    MStringAppendf(strOutput, "font: %d\n", (int)modelIndex);

    MStringAppendf(strOutput, "  scale1: %d,\n  scale2: %d,\n  radius: %d,\n  colour: #%03x,\n  newLineVector: %d\n\n",
                   (int)model->scale1, (int)model->scale2, (int)model->radius, (int)model->colour,
                   (int)model->newLineVector);

    MArrayInit(modelInfo->modelIndexes);

    DumpVerticesAndNormals((ModelData*)model, modelInfo, strOutput);

    FontModelData* fontModel = (FontModelData*) model;
    u16* offsets = fontModel->offsets;
    int i = 0;
    u16 minOffset = 0xffff;

    int maxChars = 256;
    int lastPrintableChar = 127;
    int startChars = 32;

    u16 offsetsMemberOffset = (u8*)offsets - (u8*)fontModel;
    do {
        u16 offset = offsets[i++];
        if (offset < minOffset) {
            minOffset = offset;
        }
        if (offset < debugModelParams->maxSize) {
            MStringAppend(strOutput, "\nchar: '");
            u8 c = (u8) (i + startChars);
            if (c == '\'') {
                MStringAppend(strOutput, "\\'");
            } else if (c >= lastPrintableChar) {
                MStringAppendf(strOutput, "\\%d", (int)c);
            } else {
                MStringAppendf(strOutput, "%c", (char)c);
            }
            MStringAppendf(strOutput, "' ; %d : 0x%04x \n", (int)c, (int)offset);
            u8* codeStart = ((u8*) model) + offset;
            DumpModelCode(codeStart, debugModelParams, modelInfo, strOutput);
            // Break if next offset is overlapping code we got previously
            if (offsetsMemberOffset + (i * 2) > minOffset) {
                break;
            }
        }
    } while (i  + startChars < maxChars);
}

void DecompileModelToConsole(ModelData* modelData, u32 modelIndex, ModelType modelType) {
    MMemIO writer;
    MMemInitAlloc(&writer, 100);
    DebugModelInfo modelInfo;
    DebugModelParams params;
    memset(&params,  0, sizeof(DebugModelParams));
    params.maxSize = 0xfff;
    params.onlyLabels = 1;
    if (modelType == ModelType_OBJ) {
        DecompileModel(modelData, modelIndex, &params, &modelInfo, &writer);
    } else {
        DecompileFontModel((FontModelData*)modelData, modelIndex, &params, &modelInfo, &writer);
    }
    MLog((char*)writer.mem);
    MArrayFree(modelInfo.modelIndexes);
    MMemFree(&writer);
}

typedef enum {
    ModelParser_BEGIN,
    ModelParser_COMMENT,
    ModelParser_VALUE,
    ModelParser_ERROR,
} ModelParserStateEnum;

typedef enum {
    ModelParserToken_MODEL_BEGIN,
    ModelParserToken_PAREN_OPEN,
    ModelParserToken_PAREN_CLOSE,
    ModelParserToken_SQUARE_BRACKET_OPEN,
    ModelParserToken_SQUARE_BRACKET_CLOSE,
    ModelParserToken_EQUALS,
    ModelParserToken_VALUE,
    ModelParserToken_LABEL,
    ModelParserToken_NEW_LINE,
    ModelParserToken_COMMA,
    ModelParserToken_ERROR,
    ModelParserToken_BANG,
    ModelParserToken_GREATER_THAN,
    ModelParserToken_LESS_THAN,
    ModelParserToken_MINUS,
    ModelParserToken_PLUS,
    ModelParserToken_MULT,
    ModelParserToken_DIV,
    ModelParserToken_AND,
    ModelParserToken_END,
} ModelParserTokenEnum;

MINTERNAL b32 IsAlphaNumeric(u8 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

typedef struct sModelParserContext {
    const char* textCurr;
    const char* textEnd;
    const char* valueStart;
    const char* valueEnd;
    i32 whitespaceCount;
    i32 line;
    i32 column;
    ModelParserTokenEnum token;
    ModelCompileResult result;
    MMemIO* memIO;
    ModelEndianEnum endian;
} ModelParserContext;

ModelParserTokenEnum NextTokenNoTokenUpdate(ModelParserContext* ctxt) {
    ModelParserStateEnum state = ModelParser_BEGIN;
    b32 fullLineComment = FALSE;
    while (ctxt->textCurr < ctxt->textEnd && state != ModelParser_ERROR) {
        u8 c = *ctxt->textCurr;
        ctxt->textCurr++;
        ctxt->column++;
        switch (state) {
            case ModelParser_BEGIN:
                switch (c) {
                    case ';':
                        state = ModelParser_COMMENT;
                        if (ctxt->whitespaceCount == (ctxt->column - 1)) {
                            fullLineComment = TRUE;
                        }
                        break;
                    case '\t':
                    case ' ':
                    case '\r':
                        ctxt->whitespaceCount++;
                        break;
                    case '\n':
                        ctxt->line++;
                        ctxt->column = 0;
                        ctxt->whitespaceCount = 0;
                        return ModelParserToken_NEW_LINE;
                    case ',':
                        return ModelParserToken_COMMA;
                    case '(':
                        return ModelParserToken_PAREN_OPEN;
                    case ')':
                        return ModelParserToken_PAREN_CLOSE;
                    case '[':
                        return ModelParserToken_SQUARE_BRACKET_OPEN;
                    case ']':
                        return ModelParserToken_SQUARE_BRACKET_CLOSE;
                    case '=':
                        return ModelParserToken_EQUALS;
                    case '!':
                        return ModelParserToken_BANG;
                    case '>':
                        return ModelParserToken_GREATER_THAN;
                    case '<':
                        return ModelParserToken_LESS_THAN;
                    case '+':
                        return ModelParserToken_PLUS;
                    case '-':
                        return ModelParserToken_MINUS;
                    case '/':
                        return ModelParserToken_DIV;
                    case '*':
                        return ModelParserToken_MULT;
                    case '&':
                        return ModelParserToken_AND;
                    default:
                        if (IsAlphaNumeric(c) || c == '#' || c == '.') {
                            state = ModelParser_VALUE;
                            ctxt->valueStart = ctxt->textCurr - 1;
                            ctxt->valueEnd = ctxt->valueStart;
                        } else {
                            MLog("Error");
                            return ModelParserToken_ERROR;
                        }
                        break;
                }
                break;
            case ModelParser_COMMENT:
                switch (c) {
                    case '\r':
                        break;
                    case '\n':
                        ctxt->line++;
                        ctxt->column = 0;
                        ctxt->whitespaceCount = 0;
                        if (!fullLineComment) {
                            return ModelParserToken_NEW_LINE;
                        } else {
                            state = ModelParser_BEGIN;
                        }
                        break;
                }
                break;
            case ModelParser_VALUE:
                switch (c) {
                    case ':':
                        ctxt->valueEnd = ctxt->textCurr - 1;
                        return ModelParserToken_LABEL;
                    case ' ':
                    case '\r':
                        ctxt->valueEnd = ctxt->textCurr - 1;
                        return ModelParserToken_VALUE;
                    case '\n':
                        ctxt->textCurr -= 1;
                        ctxt->valueEnd = ctxt->textCurr;
                        return ModelParserToken_VALUE;
                    case ',':
                        ctxt->textCurr -= 1;
                        ctxt->valueEnd = ctxt->textCurr;
                        return ModelParserToken_VALUE;
                    case '(':
                    case ')':
                    case '[':
                    case ']':
                    case '!':
                    case '>':
                    case '<':
                    case '=':
                    case '+':
                    case '-':
                    case '/':
                    case '*':
                        ctxt->textCurr -= 1;
                        ctxt->valueEnd = ctxt->textCurr;
                        return ModelParserToken_VALUE;
                    default:
                        if (!(IsAlphaNumeric(c) || c == '_' || c == '.')) {
                            MLog("Error");
                            return ModelParserToken_ERROR;
                        }
                        break;
                }
                break;
            default:
                MLog("Error");
                break;
        }
    }

    if (state == ModelParser_VALUE) {
        ctxt->valueEnd = ctxt->textCurr;
        return ModelParserToken_VALUE;
    }

    return ModelParserToken_END;
}

MINTERNAL ModelParserTokenEnum NextToken(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextTokenNoTokenUpdate(ctxt);
    ctxt->token = token;
    return token;
}

MINTERNAL ModelParserTokenEnum NextTokenIfNewLine(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = ctxt->token;
    if (token == ModelParserToken_NEW_LINE) {
        token = NextToken(ctxt);
    }

    return token;
}

MINTERNAL ModelParserTokenEnum NextTokenConsumeNewLine(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token == ModelParserToken_NEW_LINE) {
        token = NextToken(ctxt);
    }

    return token;
}

MINTERNAL ModelParserTokenEnum NextTokenConsumeNewLines(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = ctxt->token;
    while (token == ModelParserToken_NEW_LINE) {
        token = NextToken(ctxt);
    }

    return token;
}

ModelParserTokenEnum NextTokenSkipNewLines(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    while (token == ModelParserToken_NEW_LINE) {
        token = NextToken(ctxt);
    }

    return token;
}

ModelParserTokenEnum NextTokenConsumeLine(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    while (token != ModelParserToken_NEW_LINE && token != ModelParserToken_END) {
        token = NextToken(ctxt);
    }

    return token;
}

typedef enum {
    ModelParam_Scale1,
    ModelParam_Scale2,
    ModelParam_Radius,
    ModelParam_Colour,
} ModelParamEnum;

typedef struct sCodeLabel {
    char label[20];
    u32 codeOffset;
} CodeLabel;

MARRAY_TYPEDEF(CodeLabel, CodeLabels);

typedef struct sCodeLabelFixup {
    char label[20];
    u32 codeOffset;
    u32 fixupLocation;
    u8 fixupOffset;
} CodeLabelFixup;

MARRAY_TYPEDEF(CodeLabelFixup, CodeLabelFixups);

MINTERNAL i32 StrCopy(ModelParserContext* ctxt, char* out, i32 outSize) {
    i32 written = 0;
    const char* end = ctxt->valueEnd;
    const char* pos = ctxt->valueStart;
    outSize--;
    if (end - pos > outSize) {
        end = pos + outSize;
    }

    for (; pos < end; pos++) {
        *(out++) = *pos;
        written++;
    }

    *out = 0;

    return written;
}

MINTERNAL i32 StrCmp(ModelParserContext* ctxt, const char* str) {
    return MStrCmp3(str, ctxt->valueStart, ctxt->valueEnd);
}


#define RETURN_ERROR(msg) \
    ctxt->result.error = (char*)msg; \
    return -1;

#define RETURN_ERROR_VAL(msg) \
    ctxt->result.error = (char*)msg; \
    return -2;

MINTERNAL i32 ParseI32(ModelParserContext* ctxt, i32* val) {
    i32 r;
    b32 minus = FALSE;
    if (ctxt->token == ModelParserToken_MINUS) {
        NextToken(ctxt);
        minus = TRUE;
    }

    if (ctxt->token != ModelParserToken_VALUE) {
        RETURN_ERROR("Expecting value while parsing int");
    }

    if (*ctxt->valueStart == '#') {
        r = MParseI32Hex(ctxt->valueStart + 1, ctxt->valueEnd, val);
    } else {
        r = MParseI32(ctxt->valueStart, ctxt->valueEnd, val);
    }

    if (r == 0 && minus) {
        *val = -*val;
    }

    return r;
}

MINTERNAL i32 ParseI8(ModelParserContext* ctxt, i8* out) {
    i32 val = 0;
    i32 r = 0;
    r = ParseI32(ctxt, &val);
    if (r < 0) {
        RETURN_ERROR("Syntax error: error parsing numeric value");
    }

    if (val < -128 || val > 128) {
        RETURN_ERROR("Syntax error: int8 value out of range");
    }

    *out = (i8)val;

    return 0;
}

MINTERNAL i32 ParseU8(ModelParserContext* ctxt, u8* out) {
    i32 val = 0;
    i32 r = 0;

    if (ctxt->token != ModelParserToken_VALUE) {
        RETURN_ERROR("Expecting value while parsing int");
    }

    if (*ctxt->valueStart == '#') {
        r = MParseI32Hex(ctxt->valueStart + 1, ctxt->valueEnd, &val);
    } else {
        r = MParseI32(ctxt->valueStart, ctxt->valueEnd, &val);
    }

    if (r < 0) {
        RETURN_ERROR("Syntax error: error parsing numeric value");
    }

    if (val > 255) {
        RETURN_ERROR("Syntax error: u8 value out of range");
    }

    *out = (u8)val;

    return 0;
}

MINTERNAL i32 ParseU16(ModelParserContext* ctxt, u16* out) {
    i32 val = 0;
    i32 r = 0;

    if (ctxt->token != ModelParserToken_VALUE) {
        RETURN_ERROR("Expecting value while parsing int");
    }

    if (*ctxt->valueStart == '#') {
        r = MParseI32Hex(ctxt->valueStart + 1, ctxt->valueEnd, &val);
    } else {
        r = MParseI32(ctxt->valueStart, ctxt->valueEnd, &val);
    }

    if (r < 0) {
        RETURN_ERROR("Syntax error: error parsing u16 value");
    }

    if (val > (1 << 16)) {
        RETURN_ERROR("Syntax error: u16 value out of range");
    }

    *out = (u16)val;

    return 0;
}

enum MParseFixedFractionalHex {
    MParseFixedFractionalHex_SUCCESS = 0,
    MParseFixedFractionalHex_NOT_A_NUMBER = -1,
    MParseFixedFractionalHex_TOO_MANY_CHARS = -2,
};


i32 MParseFixedFractionalHexi32(const char* start, const char* end, i32* out) {
    i32 val = *out;
    int base = 10;

    for (const char* pos = start; pos < end; pos++) {
        char c = *pos;
        int p = 0;
        if (c >= '0' && c <= '9') {
            p = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            p = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            p = 10 + c - 'A';
        } else {
            return MParseFixedFractionalHex_NOT_A_NUMBER;
        }
        if (p) {
            if (base > 0) {
                val |= (p << base);
            } else if (base == 0) {
                val |= p;
            } else if (base > -4) {
                val |= (p >> -base);
            } else {
                return MParseFixedFractionalHex_TOO_MANY_CHARS;
            }
        }
        base -= 4;
    }

    *out = val;

    return MParseFixedFractionalHex_SUCCESS;
}

MINTERNAL i16 ParseFixed_1_14(ModelParserContext* ctxt, u16* out) {
    i16 r = 0;
    i32 val = 0;

    if (ctxt->token != ModelParserToken_VALUE) {
        RETURN_ERROR("Syntax error: expecting value while parsing fixed point number");
    }

    if (*ctxt->valueStart != '#') {
        RETURN_ERROR("Syntax error: expecting fixed point number");
    }

    i64 len = ctxt->valueEnd - ctxt->valueStart;
    if (len < 2) {
        RETURN_ERROR("Syntax error: expecting fixed point number");
    }

    char firstChar = ctxt->valueStart[1];
    if (firstChar == '1') {
        val = 0x4000;
    } else {
        val = 0;
    }

    // #1.010
    if (len > 2) {
        char periodChar = ctxt->valueStart[2];
        if (periodChar != '.') {
            RETURN_ERROR("Syntax error: error parsing fixed point number");
        }
        if (len > 3) {
            r = MParseFixedFractionalHexi32(ctxt->valueStart + 3, ctxt->valueEnd, &val);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing fixed point number");
            }
        }
    }

    *out = (u16)val;

    return 0;
}

MINTERNAL i32 ParseColour(ModelParserContext* ctxt, u16* val) {
    return ParseU16(ctxt, val);
}

MINTERNAL i32 ParseParam16Base10(ModelParserContext* ctxt, u16* outParam) {
    i32 r;
    i32 valI32;
    b32 isTmpVariable = FALSE;
    b32 isSceneVariable = FALSE;
    if (StrCmp(ctxt, "tmp") == 0) {
        isTmpVariable = TRUE;
    } else if (StrCmp(ctxt, "scene") == 0) {
        isSceneVariable = TRUE;
    } else {
        r = MParseI32(ctxt->valueStart, ctxt->valueEnd, &valI32);
        if (r) {
            ctxt->result.error = (char*)"Syntax error: parsing int";
            return r;
        }

        u16 v1 = valI32 & 0x1fe;
        u16 v2 = valI32 & 0xfe00;

        *outParam = (v1 << 7) | (v2 >> 9);

        return 0;
    }

    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_SQUARE_BRACKET_OPEN) {
        ctxt->result.error = (char*)"Syntax error: expecting [";
        ctxt->result.staticError = TRUE;
        return -1;
    }

    token = NextToken(ctxt);
    if (token != ModelParserToken_VALUE) {
        ctxt->result.error = (char*)"Syntax error: expecting value";
        return -1;
    }

    r = MParseI32(ctxt->valueStart, ctxt->valueEnd, &valI32);
    if (r) {
        ctxt->result.error = (char*)"Syntax error: parsing int";
        return r;
    }

    if (valI32 > 0x3f) {
        ctxt->result.error = (char*)"Syntax error: out of range";
        return -1;
    }

    u16 out = 0x80 | (u16)valI32;
    if (isTmpVariable) {
        out |= 0x40;
    }

    token = NextToken(ctxt);
    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
        ctxt->result.error = (char*) "Syntax error: expecting ]";
        return -1;
    }

    *outParam = out;

    return r;
}

MINTERNAL i32 ParseParam8Base10(ModelParserContext* ctxt, u8* outParam) {
    i32 r;
    i32 valI32;
    b32 isTmpVariable = FALSE;
    b32 isSceneVariable = FALSE;
    if (StrCmp(ctxt, "tmp") == 0) {
        isTmpVariable = TRUE;
    } else if (StrCmp(ctxt, "scene") == 0) {
        isSceneVariable = TRUE;
    } else {
        if (*(ctxt->valueStart) == '.') {
            r = MParseI32Hex(ctxt->valueStart + 1, ctxt->valueEnd, &valI32);
        } else {
            r = MParseI32(ctxt->valueStart, ctxt->valueEnd, &valI32);
        }
        if (r) {
            ctxt->result.error = (char*)"Syntax error: parsing int";
            return r;
        }

        if (valI32 == 0) {
            // Technically 0 is ok here as well, but the game's model code always uses a big zero
            // instead of the small one
            *outParam = 0x40;
        } else if (valI32 <= 0x3f) {
            *outParam = (u8)valI32;
        } else {
            i32 newVal = valI32 >> 10;
            if (!newVal) {
                ctxt->result.error = (char*)"Syntax error: value greater than 63 but less than 1024";
                return -1;
            }
            if (newVal > (1 << 16)) {
                ctxt->result.error = (char*)"Syntax error: value greater 2^16";
                return -1;
            }
            *outParam = 0x40 | newVal;
        }

        return 0;
    }

    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_SQUARE_BRACKET_OPEN) {
        ctxt->result.error = (char*)"Syntax error: expecting [";
        return -1;
    }

    token = NextToken(ctxt);
    if (token != ModelParserToken_VALUE) {
        ctxt->result.error = (char*)"Syntax error: expecting value";
        return -1;
    }

    r = MParseI32(ctxt->valueStart, ctxt->valueEnd, &valI32);
    if (r) {
        ctxt->result.error = (char*)"Syntax error: parsing int";
        return r;
    }

    if (valI32 > 0x3f) {
        ctxt->result.error = (char*)"Syntax error: out of range";
        return -1;
    }

    u8 out = 0x80 | (u8)valI32;
    if (isTmpVariable) {
        out |= 0x40;
    }

    token = NextToken(ctxt);
    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
        ctxt->result.error = (char*) "Syntax error: expecting ]";
        return -1;
    }

    *outParam = out;

    return r;
}

MINTERNAL void ModelWrite4i8(ModelParserContext* ctxt, i8* src) {
    if (ctxt->endian == ModelEndian_LITTLE) {
        u16 val = (((u16)((u8)src[0])) << 8) + (u8)src[1];
        MMemWriteU16LE(ctxt->memIO, val);
        val = (((u16)((u8)src[2])) << 8) + (u8)src[3];
        MMemWriteU16LE(ctxt->memIO, val);
    } else {
        MMemWriteI8CopyN(ctxt->memIO, src, 4);
    }
}

MINTERNAL void ModelWriteU16(ModelParserContext* ctxt, u16 val) {
    if (ctxt->endian == ModelEndian_LITTLE) {
        MMemWriteU16LE(ctxt->memIO, val);
    } else {
        MMemWriteU16BE(ctxt->memIO, val);
    }
}

MINTERNAL i32 ReadI8(ModelParserContext* ctxt, i8* out) {
    NextToken(ctxt);
    return ParseI8(ctxt, out);
}

MINTERNAL i32 ReadU8(ModelParserContext* ctxt, u8* out) {
    NextToken(ctxt);
    return ParseU8(ctxt, out);
}

MINTERNAL i32 ReadU16(ModelParserContext* ctxt, u16* out) {
    NextToken(ctxt);
    return ParseU16(ctxt, out);
}

MINTERNAL i32 ReadColour(ModelParserContext* ctxt, u16* val) {
    NextToken(ctxt);
    return ParseColour(ctxt, val);
}

MINTERNAL i32 ReadParam8Base10(ModelParserContext* ctxt, u8* outParam) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_VALUE) {
        RETURN_ERROR("Syntax error: expecting value");
    }

    return ParseParam8Base10(ctxt, outParam);
}

MINTERNAL i32 ReadParam16Base10(ModelParserContext* ctxt, u16* outParam) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_VALUE) {
        RETURN_ERROR("Syntax error: expecting param");
    }

    return ParseParam16Base10(ctxt, outParam);
}

MINTERNAL i32 ReadComma(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_COMMA) {
        RETURN_ERROR("Syntax error: expecting command");
    }

    return 0;
}

static i32 ReadParanOpen(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_PAREN_OPEN) {
        RETURN_ERROR("Syntax error: expecting '('");
    }

    return 0;
}

static i32 ReadParanClose(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_PAREN_CLOSE) {
        RETURN_ERROR("Syntax error: expecting ')'");
    }

    return 0;
}

static i32 ReadSquareBracketOpen(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_SQUARE_BRACKET_OPEN) {
        RETURN_ERROR("Syntax error: expecting '['");
    }

    return 0;
}

static i32 ReadSquareBracketClose(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
        RETURN_ERROR("Syntax error: expecting ']'");
    }

    return 0;
}

static i32 ReadEquals(ModelParserContext* ctxt) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_EQUALS) {
        RETURN_ERROR("Syntax error: expecting '='");
    }

    return 0;
}

#define RETURN_IF_ERROR(v) { r = v; if (r) return r; }

i32 ParseConeEnd(ModelParserContext* ctxt, b32* gotCap, i8* normal, i8* vertex, u8* radius, u16* capColour) {
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_PAREN_OPEN) {
        RETURN_ERROR("Syntax error: error expecting '('");
    }

    b32 gotRadius = FALSE;
    b32 gotNormal = FALSE;
    b32 gotVertex = FALSE;
    i32 r;

    token = NextToken(ctxt);
    while (token == ModelParserToken_LABEL) {
        if (StrCmp(ctxt, "radius") == 0 || StrCmp(ctxt, "r") == 0) {
            if (gotRadius) {
                RETURN_ERROR("Syntax error: multiple radius values");
            }
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
            }
            r = ParseU8(ctxt, radius);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing radius value");
            }
            gotRadius = TRUE;
        } else if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
            if (gotNormal) {
                RETURN_ERROR("Syntax error: multiple normal values");
            }
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
            }
            r = ParseI8(ctxt, normal);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing i8 normal value");
            }
            gotNormal = TRUE;
        } else if (StrCmp(ctxt, "vertex") == 0 || StrCmp(ctxt, "v") == 0) {
            if (gotVertex) {
                RETURN_ERROR("Syntax error: multiple vertex values");
            }
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
            }
            r = ParseI8(ctxt, vertex);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing i8 vertex value");
            }
            gotVertex = TRUE;
        } else if (StrCmp(ctxt, "c") == 0 || StrCmp(ctxt, "caps") == 0) {
            if (*gotCap) {
                RETURN_ERROR("Syntax error: multiple cap colour values");
            }
            token = NextToken(ctxt);
            if (token == ModelParserToken_SQUARE_BRACKET_CLOSE ||
                token == ModelParserToken_LABEL) {
                continue;
            }
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
            }
            r = ParseColour(ctxt, capColour);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing colour value");
            }
            *gotCap = TRUE;
        } else {
            RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
        }

        token = NextToken(ctxt);
    }

    if (!gotRadius) {
        RETURN_ERROR("Syntax error: missing 'radius' param");
    }

    if (!gotNormal) {
        RETURN_ERROR("Syntax error: missing 'normal' param");
    }

    if (!gotVertex) {
        RETURN_ERROR("Syntax error: missing 'vertex' param");
    }

    if (token != ModelParserToken_PAREN_CLOSE) {
        RETURN_ERROR("Syntax error: error expecting ')'");
    }

    return 0;
}

i32 ReadNormalAndColour(ModelParserContext* ctxt, u16* colour, i8* normal) {
    i32 r;

    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_LABEL) {
        RETURN_ERROR("Syntax error: expecting label");
    }

    b32 gotColour = FALSE;
    b32 gotNormal = FALSE;

    while (token == ModelParserToken_LABEL) {
        if (gotColour & gotNormal) {
            RETURN_ERROR("Syntax error: too many params");
        }

        if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
            }
            r = ParseColour(ctxt, colour);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing colour value");
            }
            gotColour = TRUE;
        } else if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
            }
            r = ParseI8(ctxt, normal);
            if (r < 0) {
                RETURN_ERROR("Syntax error: error parsing i8 normal value");
            }
            gotNormal = TRUE;
        } else {
            RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
        }

        token = NextToken(ctxt);
    }

    if (!gotColour) {
        RETURN_ERROR("Syntax error: missing 'colour' param");
    }

    return 0;
}

i32 ReadCircle(ModelParserContext* ctxt, b32 highlight) {
    i32 r;

    u16 type = highlight ? 0x80 : 0;

    u16 colour = 0;
    i8 vertex = 0;
    u16 radiusParam = 0;

    ModelParserTokenEnum  token = NextToken(ctxt);
    if (token != ModelParserToken_LABEL) {
        RETURN_ERROR("Syntax error: expecting label");
    }

    b32 gotColour = FALSE;
    b32 gotRadius = FALSE;
    b32 gotVertex = FALSE;

    while (token == ModelParserToken_LABEL) {
        if (gotColour & gotRadius) {
            RETURN_ERROR("Syntax error: too many params");
        }

        if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
            RETURN_IF_ERROR(ReadColour(ctxt, &colour));
            gotColour = TRUE;
        } else if (StrCmp(ctxt, "radius") == 0 || StrCmp(ctxt, "r") == 0) {
            RETURN_IF_ERROR(ReadParam16Base10(ctxt, &radiusParam));
            gotRadius = TRUE;
        } else if (StrCmp(ctxt, "vertex") == 0 || StrCmp(ctxt, "v") == 0) {
            RETURN_IF_ERROR(ReadI8(ctxt, &vertex));
            gotVertex = TRUE;
        } else {
            RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
        }

        token = NextToken(ctxt);
    }

    if (!gotColour) {
        RETURN_ERROR("Syntax error: missing 'colour' param");
    }

    if (!gotRadius) {
        RETURN_ERROR("Syntax error: missing 'radius' param");
    }

    if (!gotVertex) {
        RETURN_ERROR("Syntax error: missing 'vertex' param");
    }

    ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_CIRCLE);
    ModelWriteU16(ctxt, radiusParam);
    ModelWriteU16(ctxt, ((u16)((u8)vertex)) << 8 | type);

    return 0;
}

i32 ReadIfVariable(ModelParserContext* ctxt, u16* paramOut) {
    if (ctxt->token != ModelParserToken_VALUE) {
        RETURN_ERROR("Syntax error: expecting value");
    }
    i32 r = 0;
    if (StrCmp(ctxt, "bit") == 0) {
        u16 param = 0;
        RETURN_IF_ERROR(ReadParanOpen(ctxt));
        RETURN_IF_ERROR(ReadParam16Base10(ctxt, &param));
        RETURN_IF_ERROR(ReadComma(ctxt));
        u8 bit = 0;
        RETURN_IF_ERROR(ReadU8(ctxt, &bit));
        param |= ((u16) bit) << 8;
        RETURN_IF_ERROR(ReadParanClose(ctxt));
        *paramOut = param;
    } else {
        RETURN_IF_ERROR(ParseParam16Base10(ctxt, paramOut));
    }
    return 0;
}

i32 ReadComplex(ModelParserContext* ctxt, CodeLabelFixups* codeLabelFixups, u32 cmdStartOffset) {
    i32 r = 0;

    u16 colour = 0;
    i8 normal = 0;

    ModelParserTokenEnum  token = NextToken(ctxt);
    if (token != ModelParserToken_LABEL) {
        RETURN_ERROR("Syntax error: expecting label");
    }

    b32 gotColour = FALSE;
    b32 gotLoc = FALSE;

    while (token == ModelParserToken_LABEL) {
        if (gotColour & gotLoc) {
            RETURN_ERROR("Syntax error: too many params");
        }

        if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
            RETURN_IF_ERROR(ReadColour(ctxt, &colour));
            gotColour = TRUE;
        } else if (StrCmp(ctxt, "loc") == 0 || StrCmp(ctxt, "l") == 0) {
            NextToken(ctxt);

            CodeLabelFixup* codeLabel = MArrayAddPtr(*codeLabelFixups);
            codeLabel->codeOffset = cmdStartOffset + 3;
            codeLabel->fixupLocation = cmdStartOffset + 2;
            codeLabel->fixupOffset = 9;

            StrCopy(ctxt, codeLabel->label, 20);

            gotLoc = TRUE;
        } else if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
            RETURN_IF_ERROR(ReadI8(ctxt, &normal));
        } else {
            RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
        }

        token = NextToken(ctxt);
    }

    if (!gotColour) {
        RETURN_ERROR("Syntax error: missing 'colour' param");
    }

    if (!gotLoc) {
        RETURN_ERROR("Syntax error: missing 'loc' param");
    }

    ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_COMPLEX);
    ModelWriteU16(ctxt, ((u16)((u8)normal)));

    while (token != ModelParserToken_END) {
        token = NextToken(ctxt);
        if (token == ModelParserToken_NEW_LINE || token == ModelParserToken_END) {
            break;
        }

        if (token != ModelParserToken_VALUE) {
            RETURN_ERROR("Syntax error: Expecting complex command");
        }

        if (StrCmp(ctxt, "cbezier") == 0) {

            i8 vdata[4];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));

            ModelWriteU16(ctxt, RComplexFunc_Bezier << 9);
            ModelWriteU16(ctxt, ((u16) ((u8) vdata[0]) << 8) | (u16) ((u8) vdata[1]));
            ModelWriteU16(ctxt, ((u16) ((u8) vdata[2]) << 8) | (u16) ((u8) vdata[3]));

        } else if (StrCmp(ctxt, "cbezierc") == 0) {

            i8 vdata[3];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));

            ModelWriteU16(ctxt, ((u16)((u8)vdata[0])) | (RComplexFunc_BezierCont << 9));
            ModelWriteU16(ctxt, ((u16)((u8)vdata[1]) << 8) | (u16)((u8)vdata[2]));

        } else if (StrCmp(ctxt, "cline") == 0) {

            i8 vdata[2];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));

            ModelWriteU16(ctxt, ((u16)((u8)vdata[0])) | (RComplexFunc_Line << 9));
            ModelWriteU16(ctxt,  (u16)((u8)vdata[1]));

        } else if (StrCmp(ctxt, "clinec") == 0) {

            i8 vdata;
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata));

            ModelWriteU16(ctxt, ((u16)((u8)vdata)) | (RComplexFunc_LineCont << 9));

        } else if (StrCmp(ctxt, "cjoin") == 0) {

            ModelWriteU16(ctxt, (RComplexFunc_LineJoin << 9));

        } else if (StrCmp(ctxt, "cdone") == 0) {
            ModelWriteU16(ctxt, 0x0);
            return r;
        } else if (StrCmp(ctxt, "coval") == 0) {

            i8 vdata;
            u16 scale = 0;
            i8 normal = 0;

            b32 gotScale = FALSE;
            b32 gotNormal = FALSE;

            token = NextToken(ctxt);
            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
                    RETURN_IF_ERROR(ReadI8(ctxt, &normal));
                    gotNormal = TRUE;
                } else if (StrCmp(ctxt, "scale") == 0 || StrCmp(ctxt, "s") == 0) {
                    RETURN_IF_ERROR(ReadU16(ctxt, &scale));
                    gotScale = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotScale) {
                RETURN_ERROR("Syntax error: missing 'scale' param");
            }

            RETURN_IF_ERROR(ParseI8(ctxt, &vdata));

            ModelWriteU16(ctxt, ((u16)((u8)vdata)) | (RComplexFunc_Circle << 9));
            ModelWriteU16(ctxt,  (scale << 8) | (u16)((u8)normal));

        } else {
            RETURN_ERROR_VAL("Syntax error: unknown complex command '%s'");
        }

        if (ctxt->token != ModelParserToken_NEW_LINE) {
            token = NextToken(ctxt);
        }
    }

    return r;
}

i32 ReadFlips(ModelParserContext* ctxt, u16* flipsOut) {
    b32 minus = FALSE;
    i32 i = 0;
    char flipChars[3];
    u16 flips = 0;
    while (i < 3) {
        ModelParserTokenEnum token = NextToken(ctxt);
        if (token == ModelParserToken_VALUE) {
            for (const char* str = ctxt->valueStart; str < ctxt->valueEnd; str++) {
                if (i > 3) {
                    RETURN_ERROR_VAL("Invalid flip %s");
                }
                switch (*str) {
                    case 'x':
                        flipChars[i] = *str;
                        break;
                    case 'y':
                        flipChars[i] = *str;
                        break;
                    case 'z':
                        flipChars[i] = *str;
                        break;
                    default:
                        RETURN_ERROR_VAL("Invalid flip %s");
                }
                if (minus) {
                    flips |= (0x20 >> i);
                    minus = FALSE;
                }
                i++;
            }
        } else if (token == ModelParserToken_MINUS) {
            minus = TRUE;
        } else {
            RETURN_ERROR("Expecting flips");
        }
    }

    char* flipCharsEnd = flipChars + 3;
    if (MStrCmp3("xyz", flipChars, flipCharsEnd) == 0) {
    } else if (MStrCmp3("yzx", flipChars, flipCharsEnd) == 0) {
        flips |= 1;
    } else if (MStrCmp3("zxy", flipChars, flipCharsEnd) == 0) {
        flips |= 2;
    } else if (MStrCmp3("zyx", flipChars, flipCharsEnd) == 0) {
        flips |= 3;
    } else if (MStrCmp3("yxz", flipChars, flipCharsEnd) == 0) {
        flips |= 4;
    } else if (MStrCmp3("xzy", flipChars, flipCharsEnd) == 0) {
        flips |= 5;
    } else {
        RETURN_ERROR_VAL("Invalid flip %s");
    }

    *flipsOut = flips;
    return 0;
}

i32 ReadSubModel(ModelParserContext* ctxt) {
    // model index:130 v:2
    i32 r;
    u16 colour = 0;
    u16 index = 0;
    u16 flips = 0;
    b32 light = FALSE;
    u8 scale = 0;
    b32 frameSupplied = FALSE;
    i8 vdata[4];
    i8 vertex;
    for (int i = 0; i < 4; i++) {
        vdata[i] = 0x7f;
    }

    ModelParserTokenEnum token = NextToken(ctxt);
    if (token != ModelParserToken_LABEL) {
        RETURN_ERROR("Syntax error: expecting label");
    }

    b32 gotColour = FALSE;
    b32 gotScale = FALSE;
    b32 gotVertex = FALSE;
    b32 gotIndex = FALSE;
    b32 gotFlip = FALSE;
    b32 gotFrame = FALSE;

    while (token != ModelParserToken_END) {
        if (token == ModelParserToken_LABEL) {
            if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
                RETURN_IF_ERROR(ReadColour(ctxt, &colour));
                gotColour = TRUE;
            } else if (StrCmp(ctxt, "scale") == 0 || StrCmp(ctxt, "s") == 0) {
                RETURN_IF_ERROR(ReadParam8Base10(ctxt, &scale));
                gotScale = TRUE;
            } else if (StrCmp(ctxt, "vertex") == 0 || StrCmp(ctxt, "v") == 0) {
                RETURN_IF_ERROR(ReadI8(ctxt, &vertex));
                gotVertex = TRUE;
            } else if (StrCmp(ctxt, "index") == 0 || StrCmp(ctxt, "i") == 0) {
                RETURN_IF_ERROR(ReadU16(ctxt, &index));
                gotIndex = TRUE;
            } else if (StrCmp(ctxt, "flip") == 0) {
                RETURN_IF_ERROR(ReadFlips(ctxt, &flips));
                gotFlip = TRUE;
            } else if (StrCmp(ctxt, "frame") == 0 || StrCmp(ctxt, "f") == 0) {
                RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                int i = 0;
                do {
                    if (i >= 4) {
                        RETURN_ERROR("Too many frame parameters");
                    }
                    RETURN_IF_ERROR(ReadI8(ctxt, vdata + i));
                    token = NextToken(ctxt);
                    i++;
                } while (token == ModelParserToken_COMMA);
                if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                    RETURN_ERROR("Syntax error: expecting ']' to end frame:");
                }
                gotFrame = TRUE;
            } else {
                RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
            }
        } else if (token == ModelParserToken_VALUE) {
            if (StrCmp(ctxt, "light") == 0 || StrCmp(ctxt, "l") == 0) {
                light = TRUE;
            } else {
                RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
            }
        } else if (token == ModelParserToken_NEW_LINE) {
            break;
        } else {
            RETURN_ERROR_VAL("Syntax error: reading model '%s'");
        }

        token = NextToken(ctxt);
    }

    if (!gotVertex) {
        RETURN_ERROR("Syntax error: missing 'vertex' param");
    }

    if (!gotIndex) {
        RETURN_ERROR("Syntax error: missing 'index' param");
    }

    if (!gotFlip) {
        flips = 6;
    }

    u16 data0 = (index << 5);
    if (gotScale || gotColour) {
        data0 |= Render_MODEL_SCALE;
    } else {
        data0 |= Render_MODEL;
    }
    ModelWriteU16(ctxt, data0);

    u16 flipParams = (u8)(vertex);
    if (!light) {
        flipParams |= 0x4000;
    }
    flipParams |= (flips << 8);
    if (gotFrame) {
        flipParams |= 0x8000;
    }
    ModelWriteU16(ctxt, flipParams);

    if (gotScale || gotColour) {
        ModelWriteU16(ctxt, (u16)scale << 12 | (colour >> 1));
    }

    if (gotFrame) {
        u16 data1 = (u16)((u8)vdata[2]) << 8 | (u16)((u8)vdata[1]);
        ModelWriteU16(ctxt, data1);
        u16 data2 = (u8)((u8)vdata[3]) << 8 | (u16)((u8)vdata[0]);
        ModelWriteU16(ctxt, data2);
    }

    return 0;
}

i32 ReadRightSideCalc(ModelParserContext* ctxt, u8* paramA, u16* mathFunc, u8* paramB) {
    i32 r = 0;
    ModelParserTokenEnum token = NextToken(ctxt);
    if (token == ModelParserToken_VALUE) {
        if (StrCmp(ctxt, "min") == 0) {
            RETURN_IF_ERROR(ReadParanOpen(ctxt));
            RETURN_IF_ERROR(ReadParam8Base10(ctxt, paramA));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadParam8Base10(ctxt, paramB));
            RETURN_IF_ERROR(ReadParanClose(ctxt));
            *mathFunc = MathFunc_Min;
            return 0;
        } else if (StrCmp(ctxt, "max") == 0) {
            RETURN_IF_ERROR(ReadParanOpen(ctxt));
            RETURN_IF_ERROR(ReadParam8Base10(ctxt, paramA));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadParam8Base10(ctxt, paramB));
            RETURN_IF_ERROR(ReadParanClose(ctxt));
            *mathFunc = MathFunc_Max;
            return 0;
        }
    }

    RETURN_IF_ERROR(ParseParam8Base10(ctxt, paramA));

    token = NextToken(ctxt);
    switch (token) {
        case ModelParserToken_GREATER_THAN:
            token = NextToken(ctxt);
            if (token == ModelParserToken_GREATER_THAN) {
                *mathFunc = MathFunc_DivPower2;
            } else {
                *mathFunc = MathFunc_ZeroIfGreater;
                RETURN_IF_ERROR(ParseParam8Base10(ctxt, paramB));
                return 0;
            }
            break;
        case ModelParserToken_LESS_THAN:
            token = NextToken(ctxt);
            if (token == ModelParserToken_LESS_THAN) {
                *mathFunc = MathFunc_MultPower2;
            } else {
                *mathFunc = MathFunc_ZeroIfLess;
                RETURN_IF_ERROR(ParseParam8Base10(ctxt, paramB));
                return 0;
            }
            break;
        case ModelParserToken_MINUS:
            *mathFunc = MathFunc_Sub;
            break;
        case ModelParserToken_PLUS:
            *mathFunc = MathFunc_Add;
            break;
        case ModelParserToken_MULT:
            token = NextToken(ctxt);
            if (token == ModelParserToken_VALUE) {
                if (StrCmp(ctxt, "cos") == 0 || StrCmp(ctxt, "cosine") == 0) {
                    RETURN_IF_ERROR(ReadParanOpen(ctxt));
                    RETURN_IF_ERROR(ReadParam8Base10(ctxt, paramB));
                    RETURN_IF_ERROR(ReadParanClose(ctxt));
                    *mathFunc = MathFunc_MultCos;
                    return 0;
                } else if (StrCmp(ctxt, "sine") == 0 || StrCmp(ctxt, "sin") == 0) {
                    RETURN_IF_ERROR(ReadParanOpen(ctxt));
                    RETURN_IF_ERROR(ReadParam8Base10(ctxt, paramB));
                    RETURN_IF_ERROR(ReadParanClose(ctxt));
                    *mathFunc = MathFunc_MultSine;
                    return 0;
                }
            }
            *mathFunc = MathFunc_Mult;
            return ParseParam8Base10(ctxt, paramB);
            break;
        case ModelParserToken_DIV:
            *mathFunc = MathFunc_Div;
            break;
        case ModelParserToken_AND:
            *mathFunc = MathFunc_And;
            break;
        default:
            RETURN_ERROR("Unexpected token");
    }

    return ReadParam8Base10(ctxt, paramB);
}

enum BatchZMode {
    BatchZ_MinVertex = 0,
    BatchZ_MaxVertex = 1,
    BatchZ_VertexZ = 2,
    BatchZ_OffsetVertexZ = 3
};

i32 WriteBatchControl(ModelParserContext* ctxt, enum BatchZMode mode, u16 z) {
    i32 r;
    if (mode == BatchZ_VertexZ) {
        if (ctxt->token == ModelParserToken_LABEL && StrCmp(ctxt, "vertex") != 0) {
            RETURN_ERROR("Expect vertex param");
        }

        i8 vertex;
        RETURN_IF_ERROR(ReadI8(ctxt, &vertex));

        ModelWriteU16(ctxt, 0x4000 | (((u16)((u8)vertex)) << 5) | Render_BATCH);
    } else if (mode == BatchZ_OffsetVertexZ) {
        NextToken(ctxt);
        if (ctxt->token == ModelParserToken_LABEL && StrCmp(ctxt, "vertex") != 0) {
            RETURN_ERROR("Expect vertex param");
        }

        i8 vertex;
        RETURN_IF_ERROR(ReadI8(ctxt, &vertex));

        ModelWriteU16(ctxt, 0xe000 | (((u16)((u8)vertex)) << 5) | Render_BATCH);
        ModelWriteU16(ctxt, z);
    } else {
        ModelParserTokenEnum token = NextToken(ctxt);
        if (token == ModelParserToken_LABEL && StrCmp(ctxt, "vertex") != 0) {
            RETURN_ERROR("Expect vertex param");
        }

        u16 control = 0;
        if (mode == BatchZ_MinVertex) {
            control = 0x3;
        } else {
            control = 0x1;
        }

        RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
        token = NextToken(ctxt);

        int i = 0;
        u16 vertexIndexWide;
        while (token == ModelParserToken_VALUE || token == ModelParserToken_MINUS) {
            i8 vertexIndex;
            RETURN_IF_ERROR(ParseI8(ctxt, &vertexIndex));
            if (i >= 2) {
                // Write index from previous loop (we need to be one behind as last word needs a marker)
                ModelWriteU16(ctxt, 0xff00 | vertexIndexWide);
            }
            vertexIndexWide = (u16)((u8)vertexIndex);
            if (i == 0) {
                ModelWriteU16(ctxt, ((vertexIndexWide | (control << 8)) << 5) | Render_BATCH);
            }
            i++;
            token = NextToken(ctxt);
            if (token == ModelParserToken_COMMA) {
                token = NextToken(ctxt);
            } else if (token == ModelParserToken_SQUARE_BRACKET_CLOSE) {
                break;
            } else {
                RETURN_ERROR("Syntax error: expecting ']' or ','");
            }
        }

        if (i == 0) {
            RETURN_ERROR("Syntax error: expecting vertex index");
        }

        if (i > 1) {
            ModelWriteU16(ctxt, vertexIndexWide);
        }
    }

    return 0;
}

i32 CompileModelWithContext(ModelParserContext* ctxt, MMemIO* memOutput) {
    i32 r;

    u32 startOffset = memOutput->pos - memOutput->mem;

    MMemAddBytesZero(memOutput, sizeof(ModelData));
    ModelData* modelData = (ModelData*)(memOutput->mem + startOffset);
    ctxt->result.modelStartOffset = startOffset;

    ModelParserTokenEnum token = ctxt->token;
    if (token == ModelParserToken_MODEL_BEGIN) {
        token = NextTokenSkipNewLines(ctxt);
    }

    if (token != ModelParserToken_LABEL || StrCmp(ctxt, "model") != 0) {
        RETURN_ERROR("Syntax error: Expecting 'model:'");
    }

    token = NextToken(ctxt);
    if (token == ModelParserToken_VALUE) {
        // skip model id
        RETURN_IF_ERROR(ParseU16(ctxt, &ctxt->result.modelIndex));
        NextToken(ctxt);
    }

    token = NextTokenIfNewLine(ctxt);

    while (token == ModelParserToken_LABEL) {
        ModelParamEnum param;
        if (StrCmp(ctxt, "scale1") == 0) {
            param = ModelParam_Scale1;
        } else if (StrCmp(ctxt, "scale2") == 0) {
            param = ModelParam_Scale2;
        } else if (StrCmp(ctxt, "radius") == 0) {
            param = ModelParam_Radius;
        } else if (StrCmp(ctxt, "colour") == 0) {
            param = ModelParam_Colour;
        } else {
            RETURN_ERROR_VAL("Syntax error: unknown model param '%s'");
        }
        token = NextToken(ctxt);

        if (token != ModelParserToken_VALUE) {
            RETURN_ERROR_VAL("Syntax error: expecting value '%s'");
        }

        i32 out;
        r = ParseI32(ctxt, &out);
        if (r < 0) {
            RETURN_ERROR("Syntax error: error parsing numeric value");
        }

        switch (param) {
            case ModelParam_Scale1:
                modelData->scale1 = out;
                break;
            case ModelParam_Scale2:
                modelData->scale2 = out;
                break;
            case ModelParam_Radius:
                modelData->radius = out;
                break;
            case ModelParam_Colour:
                modelData->colour = out;
                break;
        }

        token = NextToken(ctxt);
        if (token != ModelParserToken_COMMA) {
            break;
        }
        token = NextTokenSkipNewLines(ctxt);
    }

    token = NextTokenConsumeNewLines(ctxt);

    if (token != ModelParserToken_LABEL || StrCmp(ctxt, "vertices") != 0) {
        RETURN_ERROR("Syntax error: Expecting 'vertices:'");
    }

    modelData->vertexDataOffset = sizeof(ModelData);
    int vertices = 0;

    token = NextTokenSkipNewLines(ctxt);

    i8 vdata[4];
    while (token == ModelParserToken_VALUE || token == ModelParserToken_MINUS) {
        i32 r;

        if (StrCmp(ctxt, "savg") == 0) {
            vdata[0] = 0x3;
            vdata[1] = 0x1;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
        } else if (StrCmp(ctxt, "neg") == 0) {
            vdata[0] = 0x5;
            vdata[1] = 0x1;
            vdata[2] = 0;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
        } else if (StrCmp(ctxt, "rand") == 0) {
            vdata[0] = 0x7;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
        } else if (StrCmp(ctxt, "avg") == 0) {
            vdata[0] = 0xb;
            vdata[1] = 0x1;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
        } else if (StrCmp(ctxt, "savg2") == 0) {
            vdata[0] = 0xd;
            vdata[1] = 0x1;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
        } else if (StrCmp(ctxt, "add") == 0) {
            vdata[0] = 0x11;
            vdata[1] = 0x1;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
        } else if (StrCmp(ctxt, "lerp") == 0) {
            vdata[0] = 0x13;
            u8 param;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
            RETURN_IF_ERROR(ReadComma(ctxt));

            RETURN_IF_ERROR(ReadParam8Base10(ctxt, &param));
            vdata[1] = (i8)param;

        } else {
            // Normal vertex specified with coordinates directly
            vdata[0] = 1;

            RETURN_IF_ERROR(ParseI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));
        }

        ModelWrite4i8(ctxt, vdata);

        token = NextTokenSkipNewLines(ctxt);
        vertices++;
    }

    ((ModelData*)(memOutput->mem + startOffset))->verticesDataSize = (32 * vertices) * 2;

    if (token != ModelParserToken_LABEL) {
        RETURN_ERROR("Syntax error: Expecting 'normals:' or 'code:'");
    }

    // Parse Normals
    int normals = 0;
    if (StrCmp(ctxt, "normals") == 0) {
        token = NextToken(ctxt);
        if (token != ModelParserToken_NEW_LINE) {
            RETURN_ERROR("Syntax error: Expecting new line");
        }

        ((ModelData*)(memOutput->mem + startOffset))->normalsOffset = (memOutput->pos - memOutput->mem) - startOffset;

        i8 ndata[4];
        i32 r;

        while (token != ModelParserToken_END) {
            token = NextToken(ctxt);
            if (token == ModelParserToken_NEW_LINE) {
                // Done with normals
                break;
            }

            RETURN_IF_ERROR(ParseI8(ctxt, &ndata[0]));
            RETURN_IF_ERROR(ReadParanOpen(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &ndata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &ndata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &ndata[3]));
            RETURN_IF_ERROR(ReadParanClose(ctxt));

            ModelWrite4i8(ctxt, ndata);
            normals++;

            token = NextTokenConsumeLine(ctxt);
        }

        token = NextTokenConsumeNewLines(ctxt);
        if (token != ModelParserToken_LABEL) {
            RETURN_ERROR("Syntax error: Expecting label 'code:'");
        }
    }

    ((ModelData*)(memOutput->mem + startOffset))->normalDataSize = (normals + 1) * 4;

    if (StrCmp(ctxt, "code") != 0) {
        RETURN_ERROR("Syntax error: Expecting 'code:'");
    }

    ((ModelData*)(memOutput->mem + startOffset))->codeOffset = (memOutput->pos - memOutput->mem) - startOffset;

    if (ctxt->endian == ModelEndian_LITTLE) {
        ARRAY_REWRITE_LE16(memOutput->mem + startOffset, sizeof(ModelData));
    } else {
        ARRAY_REWRITE_BE16(memOutput->mem + startOffset, sizeof(ModelData));
    }

    token = NextToken(ctxt);
    if (token != ModelParserToken_NEW_LINE) {
        RETURN_ERROR("Syntax error: Expecting new line");
    }

    b32 endedWithDone = FALSE;

    CodeLabels codeLabels;
    MArrayInit(codeLabels);

    CodeLabelFixups codeLabelFixups;
    MArrayInit(codeLabelFixups);

    // parse code
    while (token != ModelParserToken_END) {
        token = NextToken(ctxt);
        if (token == ModelParserToken_NEW_LINE || token == ModelParserToken_END) {
            break;
        }

        endedWithDone = FALSE;

        u32 cmdStartOffset = memOutput->pos - memOutput->mem;

        // Parse out code line label if any
        if (token == ModelParserToken_LABEL) {
            for (int i = 0; i < MArraySize(codeLabels); i++) {
                CodeLabel* codeLabel = MArrayGetPtr(codeLabels, i);
                if (MStrCmp3(codeLabel->label, ctxt->valueStart, ctxt->valueEnd) == 0) {
                    RETURN_ERROR_VAL("Syntax error: duplicate label '%s'");
                }
            }

            CodeLabel* codeLabel = MArrayAddPtr(codeLabels);
            codeLabel->codeOffset = cmdStartOffset;
            StrCopy(ctxt, codeLabel->label, 20);
            token = NextToken(ctxt);
        }

        if (token != ModelParserToken_VALUE) {
            RETURN_ERROR("Syntax error: Expecting command");
        }

        if (StrCmp(ctxt, "batch") == 0) {
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR("Syntax error: expecting batch start or end");
            }

            if (StrCmp(ctxt, "begin") == 0) {
                token = NextToken(ctxt);
                if (token != ModelParserToken_LABEL) {
                    RETURN_ERROR("Syntax error: expecting batch param");
                }

                if (StrCmp(ctxt, "z") == 0) {
                    token = NextToken(ctxt);
                    if (token != ModelParserToken_VALUE) {
                        RETURN_ERROR("Syntax error: expecting value");
                    }

                    if (StrCmp(ctxt, "obj") == 0) {
                        ModelWriteU16(ctxt, (0x7ff << 5) | Render_BATCH);
                    } else if (StrCmp(ctxt, "inf") == 0) {
                        ModelWriteU16(ctxt, (0x7fe << 5) | Render_BATCH);
                    } else if (StrCmp(ctxt, "min") == 0) {
                        RETURN_IF_ERROR(WriteBatchControl(ctxt, BatchZ_MinVertex, 0));
                    } else if (StrCmp(ctxt, "max") == 0) {
                        RETURN_IF_ERROR(WriteBatchControl(ctxt, BatchZ_MaxVertex, 0));
                    } else {
                        u16 z;
                        RETURN_IF_ERROR(ParseU16(ctxt, &z));
                        RETURN_IF_ERROR(WriteBatchControl(ctxt, BatchZ_OffsetVertexZ, z));
                    }
                } else {
                    RETURN_IF_ERROR(WriteBatchControl(ctxt, BatchZ_VertexZ, 0));
                }
            } else {
                ModelWriteU16(ctxt, Render_BATCH);
            }

        } else if (StrCmp(ctxt, "mident") == 0) {
            ModelWriteU16(ctxt, 0x8000 | Render_MATRIX_SETUP);
        } else if (StrCmp(ctxt, "mlight") == 0) {

            token = NextToken(ctxt);
            if (token != ModelParserToken_LABEL) {
                RETURN_ERROR("mlight requires axis parameter");
            }

            u16 axis = 0;

            if (StrCmp(ctxt, "axis") != 0 && StrCmp(ctxt, "x") == 0) {
                RETURN_ERROR("mlight requires axis parameter");
            }

            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR("expecting value for axis parameter");
            }
            if (StrCmp(ctxt, "x") == 0) {
                axis = 0;
            } else if (StrCmp(ctxt, "y") == 0) {
                axis = 1;
            } else if (StrCmp(ctxt, "z") == 0) {
                axis = 2;
            } else {
                RETURN_ERROR("expecting x, y or z");
            }

            ModelWriteU16(ctxt, (axis << 5) | Render_MATRIX_SETUP);
        } else if (StrCmp(ctxt, "mcopy") == 0) {
            ModelWriteU16(ctxt, 0xc010 | Render_MATRIX_COPY);
        } else if (StrCmp(ctxt, "balls") == 0) {
            token = NextToken(ctxt);

            u16 colour = 0;
            u16 sizeParam = 0;

            b32 gotColour = FALSE;
            b32 gotFixed = FALSE;
            b32 gotSize = FALSE;

            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
                    RETURN_IF_ERROR(ReadColour(ctxt, &colour));
                    gotColour = TRUE;
                } else if (StrCmp(ctxt, "fixed") == 0 || StrCmp(ctxt, "f") == 0) {
                    RETURN_IF_ERROR(ReadParam16Base10(ctxt, &sizeParam));
                    if (!(sizeParam & 0x80)) {
                        sizeParam |= 0x40;
                    }
                    gotFixed = TRUE;
                } else if (StrCmp(ctxt, "size") == 0 || StrCmp(ctxt, "s") == 0) {
                    RETURN_IF_ERROR(ReadParam16Base10(ctxt, &sizeParam));
                    gotSize = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown quad param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotColour) {
                RETURN_ERROR("Syntax error: balls command missing 'colour' param");
            }

            if (!gotFixed && !gotSize) {
                RETURN_ERROR("Syntax error: balls requires 'size' or 'fixed' param");
            }

            ModelWriteU16(ctxt, (colour & 0xffe) << 4 | Render_BALLS);
            ModelWriteU16(ctxt, sizeParam);

            u16 vParam = 0;
            b32 toggle = FALSE;
            while (TRUE) {
                if (token != ModelParserToken_VALUE && token != ModelParserToken_MINUS) {
                    RETURN_ERROR("Syntax error: expecting value");
                }
                i8 vertexIndex;
                r = ParseI8(ctxt, &vertexIndex);
                if (r < 0) {
                    return r;
                }
                token = NextToken(ctxt);
                if (toggle) {
                    vParam = (vParam & 0xff00) | (u8)vertexIndex;
                    ModelWriteU16(ctxt, vParam);
                } else {
                    vParam = (u8)vertexIndex;
                    vParam = (vParam << 8) | 0x7f;
                }

                if (token == ModelParserToken_NEW_LINE) {
                    if (!toggle) {
                        ModelWriteU16(ctxt, vParam);
                    } else {
                        ModelWriteU16(ctxt, 0x7f7f);
                    }
                    break;
                }

                if (token != ModelParserToken_COMMA) {
                    RETURN_ERROR("Syntax error: expecting comma");
                }

                toggle = !toggle;

                token = NextToken(ctxt);
            }

        } else if (StrCmp(ctxt, "if") == 0) {
            token = NextToken(ctxt);
            b32 invert = FALSE;
            u16 param1 = 0;
            u16 param2 = 0;

            if (token == ModelParserToken_BANG) {
                invert = TRUE;
                NextToken(ctxt);
                RETURN_IF_ERROR(ReadIfVariable(ctxt, &param2));
                param1 = invert ? Render_IF_NOT_VAR : Render_IF_VAR;
            } else if (token == ModelParserToken_LESS_THAN || token == ModelParserToken_GREATER_THAN) {
                if (token == ModelParserToken_LESS_THAN) {
                    invert = TRUE;
                }
                token = NextToken(ctxt);

                if (token != ModelParserToken_LABEL) {
                    RETURN_ERROR("Syntax error: expecting 'z' or 'normal' param");
                }

                param1 = invert ? Render_IF_NOT : Render_IF;

                if (StrCmp(ctxt, "z") == 0) {
                    param2 = 0;
                    RETURN_IF_ERROR(ReadU16(ctxt, &param2));
                    param2 <<= 1;
                } else if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
                    param2 = 0x8000;
                    u8 normal;
                    RETURN_IF_ERROR(ReadU8(ctxt, &normal));
                    param2 |= normal;
                } else {
                    RETURN_ERROR("Syntax error: expecting 'z' or 'normal' param");
                }
            } else {
                RETURN_IF_ERROR(ReadIfVariable(ctxt, &param2));
                param1 = invert ? Render_IF_NOT_VAR : Render_IF_VAR;
            }

            token = NextToken(ctxt);
            if ((token == ModelParserToken_VALUE && StrCmp(ctxt, "end") != 0) ||
                (token == ModelParserToken_LABEL && StrCmp(ctxt, "loc") != 0)) {
                RETURN_ERROR("Syntax error: expecting 'loc:<label>' or 'end'");
            }

            ModelWriteU16(ctxt, param1);
            ModelWriteU16(ctxt, param2);

            if (token == ModelParserToken_LABEL) {
                token = NextToken(ctxt);

                CodeLabelFixup* codeLabel = MArrayAddPtr(codeLabelFixups);
                codeLabel->codeOffset = memOutput->pos - memOutput->mem;
                codeLabel->fixupLocation = cmdStartOffset;
                codeLabel->fixupOffset = 5;
                StrCopy(ctxt, codeLabel->label, 20);
            }
        } else if (StrCmp(ctxt, "circle") == 0) {
            RETURN_IF_ERROR(ReadCircle(ctxt, FALSE));
        } else if (StrCmp(ctxt, "highlight") == 0) {
            RETURN_IF_ERROR(ReadCircle(ctxt, TRUE));
        } else if (StrCmp(ctxt, "line") == 0) {
            i8 vdata[2];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));

            u16 colour = 0;

            token = NextToken(ctxt);
            if (token != ModelParserToken_LABEL) {
                RETURN_ERROR("Syntax error: expecting label");
            }

            if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
                RETURN_IF_ERROR(ReadColour(ctxt, &colour));
            } else {
                RETURN_ERROR_VAL("Syntax error: unknown line param '%s'");
            }

            ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_LINE);
            ModelWriteU16(ctxt, (u16)((u8)vdata[0]) << 8 | (u16)((u8)vdata[1]));

        } else if (StrCmp(ctxt, "bline") == 0) {
            i8 vdata[4];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));

            u16 colour = 0;
            i8 normal = 0;
            RETURN_IF_ERROR(ReadNormalAndColour(ctxt, &colour, &normal));

            ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_LINE_BEZIER);
            ModelWriteU16(ctxt, (u16)((u8)vdata[0]) << 8 | (u16)((u8)vdata[1]));
            ModelWriteU16(ctxt, (u16)((u8)vdata[2]) << 8 | (u16)((u8)vdata[3]));
            ModelWriteU16(ctxt, (u16)normal);

        } else if (StrCmp(ctxt, "tri") == 0) {
            i8 vdata[3];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));

            u16 colour = 0;
            i8 normal = 0;
            r = ReadNormalAndColour(ctxt, &colour, &normal);
            if (r) {
                return r;
            }

            ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_TRI);
            ModelWriteU16(ctxt, (u16)((u8)vdata[0]) << 8 | (u16)((u8)vdata[1]));
            ModelWriteU16(ctxt, (u16)((u8)vdata[2]) << 8 | (u16)((u8)normal));

        } else if (StrCmp(ctxt, "mtri") == 0) {
            i8 vdata[3];
            i32 r;

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));

            u16 colour = 0;
            i8 normal = 0;
            RETURN_IF_ERROR(ReadNormalAndColour(ctxt, &colour, &normal));

            ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_MIRRORED_TRI);
            ModelWriteU16(ctxt, (u16)((u8)vdata[0]) << 8 | (u16)((u8)vdata[1]));
            ModelWriteU16(ctxt, (u16)((u8)vdata[2]) << 8 | (u16)((u8)normal));

        } else if (StrCmp(ctxt, "quad") == 0) {

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));

            u16 colour = 0;
            i8 normal = 0;
            r = ReadNormalAndColour(ctxt, &colour, &normal);
            if (r) {
                return r;
            }

            ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_QUAD);
            ModelWriteU16(ctxt, (u16)((u8)vdata[0]) << 8 | (u16)((u8)vdata[1]));
            ModelWriteU16(ctxt, (u16)((u8)vdata[2]) << 8 | (u16)((u8)vdata[3]));
            ModelWriteU16(ctxt, (u16)((u8)normal));

        } else if (StrCmp(ctxt, "mquad") == 0) {
            i8 vdata[4];

            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[2]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[3]));

            u16 colour = 0;
            i8 normal = 0;
            r = ReadNormalAndColour(ctxt, &colour, &normal);
            if (r) {
                return r;
            }

            ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_MIRRORED_QUAD);
            ModelWriteU16(ctxt, (u16)((u8)vdata[0]) << 8 | (u16)((u8)vdata[1]));
            ModelWriteU16(ctxt, (u16)((u8)vdata[2]) << 8 | (u16)((u8)vdata[3]));
            ModelWriteU16(ctxt, (u16)((u8)normal));

        } else if (StrCmp(ctxt, "cone") == 0) {
            u16 colour = 0;

            token = NextToken(ctxt);
            if (token != ModelParserToken_LABEL) {
                RETURN_ERROR("Syntax error: expecting colour param");
            }

            if (StrCmp(ctxt, "colour") != 0 && StrCmp(ctxt, "c") != 0) {
                RETURN_ERROR("Syntax error: expecting colour param");
            }

            RETURN_IF_ERROR(ReadColour(ctxt, &colour));

            b32 gotCap1 = FALSE;
            b32 gotCap2 = FALSE;
            i8 normal1 = 0;
            i8 vertex1 = 0;
            u8 radius1 = 0;
            u16 capColour1 = 0;

            r = ParseConeEnd(ctxt, &gotCap1, &normal1, &vertex1, &radius1, &capColour1);
            if (r < 0) {
                return r;
            }

            i8 normal2 = 0;
            i8 vertex2 = 0;
            u8 radius2 = 0;
            u16 capColour2 = 0;

            r = ParseConeEnd(ctxt, &gotCap2, &normal2, &vertex2, &radius2, &capColour2);
            if (r < 0) {
                return r;
            }

            b32 altColourCaps = (gotCap1 | gotCap2) &&
                                 (!gotCap1 || capColour1 != colour) &&
                                 (!gotCap2 || capColour2 != colour);

            if (altColourCaps) {
                ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_CONE_COLOUR_CAP);
            } else {
                ModelWriteU16(ctxt, (colour  & 0xffe) << 4 | Render_CONE);
            }

            ModelWriteU16(ctxt, ((u16)((u8)vertex2) << 8) | (u8)vertex1);
            u16 data2 = ((u16)((u8)radius1) << 8) | (u8)normal1;
            if (gotCap1) {
                data2 |= 0x80;
            }
            ModelWriteU16(ctxt, data2);
            u16 data3 = ((u16)((u8)radius2) << 8) | (u8)normal2;
            if (gotCap2) {
                data3 |= 0x80;
            }
            ModelWriteU16(ctxt, data3);
            if (altColourCaps) {
                ModelWriteU16(ctxt, capColour1);
                ModelWriteU16(ctxt, capColour2);
            }

        } else if (StrCmp(ctxt, "model") == 0) {
            RETURN_IF_ERROR(ReadSubModel(ctxt));

        } else if (StrCmp(ctxt, "complex") == 0) {
            RETURN_IF_ERROR(ReadComplex(ctxt, &codeLabelFixups, cmdStartOffset));

            token = NextToken(ctxt);
        } else if (StrCmp(ctxt, "ztree") == 0) {
            token = NextToken(ctxt);
            if (token != ModelParserToken_VALUE) {
                RETURN_ERROR("Syntax error: ztree needs value 'pop' or 'push'");
            }

            if (StrCmp(ctxt, "pop") == 0) {
                ModelWriteU16(ctxt, 0x8000 | Render_CLEARZ);
            } else if (StrCmp(ctxt, "push") == 0) {
                token = NextToken(ctxt);
                if (token != ModelParserToken_LABEL || ((StrCmp(ctxt, "vertex") != 0) && (StrCmp(ctxt, "v") != 0))) {
                    RETURN_ERROR_VAL("Syntax error: ztree push requires 'v:' param");
                }

                i8 index;
                RETURN_IF_ERROR(ReadI8(ctxt, &index));
                ModelWriteU16(ctxt, ((u16)((u8)index) << 5) | Render_CLEARZ);
            } else {
                RETURN_ERROR_VAL("Syntax error: ztree '%s' should be 'push' or 'pop'");
            }

            token = NextToken(ctxt);
        } else if (StrCmp(ctxt, "tmp") == 0) {
            u16 resultOffset = 0;

            RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
            RETURN_IF_ERROR(ReadU16(ctxt, &resultOffset));
            RETURN_IF_ERROR(ReadSquareBracketClose(ctxt));
            RETURN_IF_ERROR(ReadEquals(ctxt));

            u8 paramA = 0;
            u16 mathFunc = 0;
            u8 paramB = 0;

            RETURN_IF_ERROR(ReadRightSideCalc(ctxt, &paramA, &mathFunc, &paramB));

            resultOffset |= 0xc0; // tmp variable write, byte code allows for scene write as well technically, but
                                  // unused and render code does not support

            ModelWriteU16(ctxt, ((resultOffset << 4) | mathFunc) << 4 | Render_CALC_A);
            ModelWriteU16(ctxt, (((u16)paramB) << 8) | paramA);

            token = NextToken(ctxt);
        } else if (StrCmp(ctxt, "planet") == 0) {
            token = NextToken(ctxt);

            i8 vi = 0;
            u16 colours[16];
            u16 numColours = 0;
            u16 bandWidths[16];
            u16 bandColours[16];
            u16 numBands = 0;
            u16 atmosColour = 0;
            u16 size = 0;
            u16 shade = 0;
            i8 featuresByteCode[512];
            int featuresSize = 0;
            int gotPaletteSelect = FALSE;
            int gotPalette = FALSE;

            b32 gotSize = FALSE;
            b32 gotVertex = FALSE;
            b32 gotColours = FALSE;
            b32 gotAtmosColour = FALSE;
            b32 gotBands = FALSE;
            b32 gotShade = FALSE;
            b32 gotFeatures = FALSE;
            u8 paletteSelect = 0;
            u16 palette[4 * 7];

            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "size") == 0 || StrCmp(ctxt, "s") == 0) {
                    RETURN_IF_ERROR(ReadU16(ctxt, &size));
                    gotSize = TRUE;
                } else if (StrCmp(ctxt, "shade") == 0 || StrCmp(ctxt, "sh") == 0) {
                    RETURN_IF_ERROR(ReadU16(ctxt, &shade));
                    switch (shade) {
                        case 1:
                            shade = 0x8;
                            break;
                        case 2:
                            shade = 0xc;
                            break;
                        case 3:
                            shade = 0xe;
                            break;
                        default:
                            RETURN_ERROR("shade must be 1, 2 or 3");
                    }
                    gotShade = TRUE;
                } else if (StrCmp(ctxt, "vertex") == 0 || StrCmp(ctxt, "v") == 0) {
                    RETURN_IF_ERROR(ReadI8(ctxt, &vi));
                    gotVertex = TRUE;
                } else if (StrCmp(ctxt, "atmosColour") == 0 || StrCmp(ctxt, "a") == 0) {
                    RETURN_IF_ERROR(ReadColour(ctxt, &atmosColour));
                    gotAtmosColour = TRUE;
                } else if (StrCmp(ctxt, "paletteSelect") == 0) {
                    RETURN_IF_ERROR(ReadParam8Base10(ctxt, &paletteSelect));
                    gotPaletteSelect = TRUE;
                } else if (StrCmp(ctxt, "palette") == 0) {
                    u16* palettePtr = palette;
                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                    for (int k = 0; k < 7; k++) {
                        RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                        RETURN_IF_ERROR(ReadColour(ctxt, palettePtr++));
                        RETURN_IF_ERROR(ReadComma(ctxt));
                        RETURN_IF_ERROR(ReadColour(ctxt, palettePtr++));
                        RETURN_IF_ERROR(ReadComma(ctxt));
                        RETURN_IF_ERROR(ReadColour(ctxt, palettePtr++));
                        RETURN_IF_ERROR(ReadComma(ctxt));
                        RETURN_IF_ERROR(ReadColour(ctxt, palettePtr++));
                        RETURN_IF_ERROR(ReadSquareBracketClose(ctxt));
                        if (k != 6) {
                            RETURN_IF_ERROR(ReadComma(ctxt));
                        }
                    }
                    RETURN_IF_ERROR(ReadSquareBracketClose(ctxt));
                    gotPalette = TRUE;
                } else if (StrCmp(ctxt, "colours") == 0 || StrCmp(ctxt, "c") == 0) {
                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                    int i = 0;
                    do {
                        if (i > 16) {
                            RETURN_ERROR("Too many colour parameters");
                        }
                        RETURN_IF_ERROR(ReadColour(ctxt, colours + i));
                        token = NextToken(ctxt);
                        numColours++;
                        i++;
                    } while (token == ModelParserToken_COMMA);
                    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                        RETURN_ERROR("Syntax error: expecting ']'");
                    }
                    gotColours = TRUE;
                } else if (StrCmp(ctxt, "atmosBands") == 0 || StrCmp(ctxt, "b") == 0) {
                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                    int i = 0;
                    do {
                        if (i > 16) {
                            RETURN_ERROR("Too many atmosBand parameters");
                        }
                        NextToken(ctxt);
                        RETURN_IF_ERROR(ParseFixed_1_14(ctxt, bandWidths + i));
                        RETURN_IF_ERROR(ReadColour(ctxt, bandColours + i));
                        token = NextToken(ctxt);
                        numBands++;
                        i++;
                    } while (token == ModelParserToken_COMMA);
                    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                        RETURN_ERROR("Syntax error: expecting ']'");
                    }
                    gotBands = TRUE;
                } else if (StrCmp(ctxt, "features") == 0) {
                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                    do {
                        RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));

                        i8 featureControl = 0;
                        RETURN_IF_ERROR(ReadI8(ctxt, &featureControl));

                        *(featuresByteCode + (featuresSize++)) = featureControl;

                        if (featureControl < 0) {
                            // Render surface circle on sphere at given point
                            RETURN_IF_ERROR(ReadComma(ctxt));
                            RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                            RETURN_IF_ERROR(ReadComma(ctxt));
                            RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                            RETURN_IF_ERROR(ReadComma(ctxt));
                            RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                            RETURN_IF_ERROR(ReadComma(ctxt));
                            RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));

                            token = NextToken(ctxt);
                            if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                                RETURN_ERROR("Syntax error: expecting ']'");
                            }
                        } else {
                            RETURN_IF_ERROR(ReadComma(ctxt));
                            RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                            token = NextToken(ctxt);
                            if (token == ModelParserToken_SQUARE_BRACKET_CLOSE) {
                                // Marker
                                featuresByteCode[featuresSize++] = 0;
                            } else {
                                // Render complex surface poly on sphere
                                if (token != ModelParserToken_COMMA) {
                                    RETURN_ERROR("Syntax error: expecting comma");
                                }

                                do {
                                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                                    RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                                    RETURN_IF_ERROR(ReadComma(ctxt));
                                    RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                                    RETURN_IF_ERROR(ReadComma(ctxt));
                                    RETURN_IF_ERROR(ReadI8(ctxt, featuresByteCode + (featuresSize++)));
                                    RETURN_IF_ERROR(ReadSquareBracketClose(ctxt));

                                    token = NextToken(ctxt);
                                } while (token == ModelParserToken_COMMA);

                                if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                                    RETURN_ERROR("Syntax error: expecting ']'");
                                }
                                featuresByteCode[featuresSize++] = 0;
                            }
                        }

                        featuresByteCode[featuresSize] = 0;
                        token = NextToken(ctxt);
                    } while (token == ModelParserToken_COMMA);

                    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                        RETURN_ERROR("Syntax error: expecting ']'");
                    }

                    gotFeatures = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotSize) {
                RETURN_ERROR("Syntax error: missing 'size' param");
            }

            if (!gotVertex) {
                RETURN_ERROR("Syntax error: missing 'vertex' param");
            }

            if (!gotColours) {
                RETURN_ERROR("Syntax error: missing 'colours' param");
            }

            if (numColours < 4) {
                RETURN_ERROR("Syntax error: too few 'colours' - either 4 or 8 colours must be supplied");
            }

            if (numColours != 4 && numColours != 8) {
                RETURN_ERROR("Syntax error: either 4 or 8 colours must be supplied");
            }

            if (featuresSize) {
                if (featuresSize & 0x1) {
                    featuresByteCode[featuresSize] = 0;
                    featuresSize += 1;
                }
            }

            u16 colourParam = shade;
            if (gotPaletteSelect && gotPalette) {
                colourParam |= 0x1;
                if (numColours > 4) {
                    RETURN_ERROR("Syntax error: cannot add palette when 8 colours already specified");
                }
            } else if (gotPaletteSelect && !gotPalette) {
                RETURN_ERROR("Syntax error: missing palette list but got paletteSelect");
            } else if (!gotPaletteSelect && gotPalette) {
                RETURN_ERROR("Syntax error: missing paletteSelect but got palette");
            } else if (numColours == 4) {
                colourParam |= 0x4;
            }

            // command size in words minus first 3
            u16 dataSize = numColours + 1 + (numBands * 2) + 1 + (featuresSize >> 1);
            if (gotPaletteSelect) {
                dataSize += 1;
            }
            if (gotPalette) {
                dataSize += 4 * 7;
            }

            // command size in words minus first 3
            ModelWriteU16(ctxt, (dataSize << 5) | Render_PLANET);
            ModelWriteU16(ctxt, size);
            ModelWriteU16(ctxt, ((u16)((u8)vi)));
            ModelWriteU16(ctxt, (colourParam << 12) | colours[0]);

            for (int i = 1; i < numColours; i++) {
                ModelWriteU16(ctxt, colours[i]);
            }

            if (gotPaletteSelect) {
                ModelWriteU16(ctxt, paletteSelect);
            }

            if (gotPalette) {
                for (int i = 0; i < 4 * 7; i++) {
                    ModelWriteU16(ctxt, palette[i]);
                }
            }

            u16 atmosParam = atmosColour;
            if (gotBands && !atmosParam) {
                atmosParam |= 1;
            }

            ModelWriteU16(ctxt, atmosParam);

            if (gotBands) {
                for (int i = 0; i < numBands; i++) {
                    ModelWriteU16(ctxt, bandWidths[i]);
                    ModelWriteU16(ctxt, bandColours[i]);
                }
                ModelWriteU16(ctxt, 0);
            }

            if (featuresSize) {
                MMemWriteI8CopyN(ctxt->memIO, featuresByteCode, featuresSize);
            }

            ModelWriteU16(ctxt, 0);

        } else if (StrCmp(ctxt, "teardrop") == 0) {
            token = NextToken(ctxt);

            u16 colour = 0;
            u16 radius = 0;

            b32 gotRadius = FALSE;
            b32 gotColour = FALSE;

            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "radius") == 0 || StrCmp(ctxt, "r") == 0) {
                    RETURN_IF_ERROR(ReadU16(ctxt, &radius));
                    gotRadius = TRUE;
                } else if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
                    RETURN_IF_ERROR(ReadColour(ctxt, &colour));
                    gotColour = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotColour) {
                RETURN_ERROR("Syntax error: missing 'colour' param");
            }

            if (!gotRadius) {
                RETURN_ERROR("Syntax error: missing 'radius' param");
            }

            RETURN_IF_ERROR(ParseI8(ctxt, &vdata[0]));
            RETURN_IF_ERROR(ReadComma(ctxt));
            RETURN_IF_ERROR(ReadI8(ctxt, &vdata[1]));

            ModelWriteU16(ctxt, ((colour & 0xffe) << 4) | Render_TEARDROP);
            ModelWriteU16(ctxt, (u16)((u8)vdata[1]) << 8 | (u16)((u8)vdata[0]));
            ModelWriteU16(ctxt, radius);

        } else if (StrCmp(ctxt, "vtext") == 0) {
            token = NextToken(ctxt);

            u16 colour = 0;
            i8 vertex = 0;
            u8 normalIndex = 0;
            u8 transform = 0;
            u8 scale = 0;
            u8 fontIndex = 0;
            u8 stringIndex = 0;
            u16 stringType = 0;

            b32 gotVertex = FALSE;
            b32 gotColour = FALSE;
            b32 gotTransform = FALSE;
            b32 gotNormal = FALSE;
            b32 gotScale = FALSE;
            b32 gotFontIndex = FALSE;
            b32 gotStringIndex = FALSE;

            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "vertex") == 0 || StrCmp(ctxt, "v") == 0) {
                    RETURN_IF_ERROR(ReadI8(ctxt, &vertex));
                    gotVertex = TRUE;
                } else if (StrCmp(ctxt, "colour") == 0 || StrCmp(ctxt, "c") == 0) {
                    RETURN_IF_ERROR(ReadColour(ctxt, &colour));
                    gotColour = TRUE;
                } else if (StrCmp(ctxt, "transform") == 0 || StrCmp(ctxt, "t") == 0) {
                    RETURN_IF_ERROR(ReadU8(ctxt, &transform));
                    gotTransform = TRUE;
                } else if (StrCmp(ctxt, "scale") == 0 || StrCmp(ctxt, "s") == 0) {
                    RETURN_IF_ERROR(ReadU8(ctxt, &scale));
                    gotScale = TRUE;
                } else if (StrCmp(ctxt, "font") == 0 || StrCmp(ctxt, "f") == 0) {
                    RETURN_IF_ERROR(ReadU8(ctxt, &fontIndex));
                    gotFontIndex = TRUE;
                } else if (StrCmp(ctxt, "string") == 0 || StrCmp(ctxt, "i") == 0) {
                    token = NextToken(ctxt);
                    if (token != ModelParserToken_VALUE) {
                        RETURN_ERROR("Expected string qualifier")
                    }
                    if (StrCmp(ctxt, "module") == 0) {
                        stringType = 0x4000;
                    } else if (StrCmp(ctxt, "model") == 0) {
                        stringType = 0x3000;
                    } else {
                        RETURN_ERROR("Unknown string type");
                    }
                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                    RETURN_IF_ERROR(ReadU8(ctxt, &stringIndex));
                    RETURN_IF_ERROR(ReadSquareBracketClose(ctxt));
                    gotStringIndex = TRUE;

                } else if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
                    RETURN_IF_ERROR(ReadU8(ctxt, &normalIndex));
                    gotNormal = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotVertex) {
                RETURN_ERROR("Syntax error: missing 'vertex' param");
            }

            if (!gotColour) {
                RETURN_ERROR("Syntax error: missing 'colour' param");
            }

            if (!gotTransform) {
                RETURN_ERROR("Syntax error: missing 'transform' param");
            }

            if (!gotScale) {
                RETURN_ERROR("Syntax error: missing 'scale' param");
            }

            if (!gotFontIndex) {
                RETURN_ERROR("Syntax error: missing 'font' param");
            }

            if (!gotStringIndex) {
                RETURN_ERROR("Syntax error: missing 'string' param");
            }

            ModelWriteU16(ctxt, ((colour & 0xffe) << 4) | Render_VTEXT);
            ModelWriteU16(ctxt, ((u16)((u8)fontIndex) << 12) | (((u16)(scale)) << 8) | (u16)((u8)normalIndex));
            ModelWriteU16(ctxt, ((u16)(transform)) << 8 | ((u16)((u8)vertex)));
            ModelWriteU16(ctxt, stringType | (u16)stringIndex);

        } else if (StrCmp(ctxt, "mrotate") == 0) {

            token = NextToken(ctxt);

            u16 angle = 0;
            u16 flips = 0;
            u16 axis = 0;

            b32 gotAxis = FALSE;
            b32 gotAngle = FALSE;
            b32 gotFlips = FALSE;

            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "axis") == 0 || StrCmp(ctxt, "x") == 0) {
                    token = NextToken(ctxt);
                    if (token != ModelParserToken_VALUE) {
                        RETURN_ERROR("expecting value");
                    }
                    if (StrCmp(ctxt, "x") == 0) {
                        axis = 0;
                    } else if (StrCmp(ctxt, "y") == 0) {
                        axis = 1;
                    } else if (StrCmp(ctxt, "z") == 0) {
                        axis = 2;
                    } else {
                        RETURN_ERROR("expecting x, y or z");
                    }
                    gotAxis = TRUE;
                } else if (StrCmp(ctxt, "angle") == 0 || StrCmp(ctxt, "a") == 0) {
                    RETURN_IF_ERROR(ReadParam16Base10(ctxt, &angle));
                    gotAngle = TRUE;
                } else if (StrCmp(ctxt, "flip") == 0 || StrCmp(ctxt, "f") == 0) {
                    RETURN_IF_ERROR(ReadFlips(ctxt, &flips));
                    gotFlips = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown quad param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotAxis) {
                RETURN_ERROR("Syntax error: missing 'axis' param");
            }

            if (!gotAngle) {
                RETURN_ERROR("Syntax error: missing 'angle' param");
            }

            u16 flipsParam = axis << 5;
            if (gotFlips) {
                flipsParam |= (flips << 7);
            } else {
                flipsParam |= 0x8300;
            }

            ModelWriteU16(ctxt, flipsParam | Render_MATRIX_TRANSFORM);
            ModelWriteU16(ctxt, angle);
        } else if (StrCmp(ctxt, "msetup") == 0) {
            ModelWriteU16(ctxt, Render_MATRIX_COPY);

        } else if (StrCmp(ctxt, "colour") == 0) {
            token = NextToken(ctxt);

            u8 indexParam = 0;
            u16 colours[8];
            u16 numColours = 0;
            u8 normal = 0;

            b32 gotColours = FALSE;
            b32 gotIndex = FALSE;
            b32 gotNormal = FALSE;

            while (token == ModelParserToken_LABEL) {
                if (StrCmp(ctxt, "colours") == 0 || StrCmp(ctxt, "c") == 0) {
                    RETURN_IF_ERROR(ReadSquareBracketOpen(ctxt));
                    int i = 0;
                    do {
                        if (i > 8) {
                            RETURN_ERROR("Too many colour parameters");
                        }
                        RETURN_IF_ERROR(ReadColour(ctxt, colours + i));
                        token = NextToken(ctxt);
                        numColours++;
                        i++;
                    } while (token == ModelParserToken_COMMA);
                    if (token != ModelParserToken_SQUARE_BRACKET_CLOSE) {
                        RETURN_ERROR("Syntax error: expecting ']'");
                    }
                    gotColours = TRUE;
                } else if (StrCmp(ctxt, "index") == 0 || StrCmp(ctxt, "i") == 0) {
                    RETURN_IF_ERROR(ReadParam8Base10(ctxt, &indexParam));
                    gotIndex = TRUE;
                } else if (StrCmp(ctxt, "normal") == 0 || StrCmp(ctxt, "n") == 0) {
                    RETURN_IF_ERROR(ReadU8(ctxt, &normal));
                    gotNormal = TRUE;
                } else {
                    RETURN_ERROR_VAL("Syntax error: unknown quad param '%s'");
                }

                token = NextToken(ctxt);
            }

            if (!gotColours) {
                RETURN_ERROR("Syntax error: missing 'colours' param");
            }

            if (!numColours) {
                RETURN_ERROR("colours command requires 1 or more colours");
            }

            ModelWriteU16(ctxt, ((colours[0] << 4) & 0xffe0) | Render_COLOUR);

            if (numColours == 1) {
                ModelWriteU16(ctxt, normal);
            } else {
                ModelWriteU16(ctxt, (((u16) indexParam) << 8) | normal);
                for (int i = 1; i < 8; i++) {
                    u16 colour = 0;
                    if (i < numColours) {
                        colour = colours[i];
                    } else {
                        colour = colours[numColours - 1];
                    }
                    ModelWriteU16(ctxt, colour);
                }
            }

        } else if (StrCmp(ctxt, "done") == 0) {
            ModelWriteU16(ctxt, 0x0);
            endedWithDone = TRUE;
        } else {
            RETURN_ERROR_VAL("Syntax error: unknown model command '%s'");
        }

        if (ctxt->token != ModelParserToken_NEW_LINE) {
            token = NextToken(ctxt);
        }
    }

    for (int i = 0; i < MArraySize(codeLabelFixups); i++) {
        CodeLabelFixup* codeLabelFixup = MArrayGetPtr(codeLabelFixups, i);

        for (int j = 0; j < MArraySize(codeLabels); j++) {
            CodeLabel* codeLabel = MArrayGetPtr(codeLabels, j);
            if (MStrCmp(codeLabelFixup->label, codeLabel->label) == 0) {
                i32 offset = (codeLabel->codeOffset - codeLabelFixup->codeOffset) /2;
                u16 offsetWord;
                if (ctxt->endian == ModelEndian_LITTLE) {
                    offsetWord = MLITTLEENDIAN16(offset << codeLabelFixup->fixupOffset);
                } else {
                    offsetWord = MBIGENDIAN16(offset << codeLabelFixup->fixupOffset);
                }
                *((u16*)( memOutput->mem + codeLabelFixup->fixupLocation)) |= offsetWord;
            }
        }
    }

    if (!endedWithDone) {
        ModelWriteU16(ctxt, 0x0);
    }

    MArrayFree(codeLabels);
    MArrayFree(codeLabelFixups);

    return 0;
}

void BuildErrorStringWithValue(ModelParserContext* ctxt) {
    u32 valLen = ctxt->valueEnd - ctxt->valueStart;
    u32 len = strlen(ctxt->result.error) + valLen;
    char* error = (char*)MMalloc(len);
    char* valueParam = (char*)MMalloc(len);
    u32 i = 0;
    for (; i < valLen; ++i) {
        valueParam[i] = ctxt->valueStart[i];
    }
    valueParam[i] = 0;
    snprintf(error, len, ctxt->result.error, valueParam);
    MFree(valueParam);
    ctxt->result.error = error;
    ctxt->result.staticError = FALSE;
}

ModelCompileResult CompileModel(const char* dataIn, u32 sizeIn, MMemIO* memOutput) {
    ModelParserContext parserContext;
    parserContext.textCurr = dataIn;
    parserContext.textEnd = dataIn + sizeIn;
    parserContext.valueStart = NULL;
    parserContext.valueEnd = NULL;
    parserContext.line = 1;
    parserContext.column = 0;
    parserContext.token = ModelParserToken_MODEL_BEGIN;
    parserContext.memIO = memOutput;
    parserContext.endian = ModelEndian_LITTLE;
    parserContext.result.error = NULL;
    parserContext.result.modelIndex = 0;

    i32 r = CompileModelWithContext(&parserContext, memOutput);

    if (r) {
        if (r == -2) {
            BuildErrorStringWithValue(&parserContext);
        }
        parserContext.result.errorLine = parserContext.line;
        parserContext.result.errorColumn = parserContext.column;
    }

    return parserContext.result;
}

typedef struct {
    u32 modelIndex;
    u32 offset;
} ModelOffset;

MARRAY_TYPEDEF(ModelOffset, ModelOffsetsArray)

ModelCompileResult CompileMultipleModels(const char* dataIn, u32 sizeIn, MMemIO* memOutput, ModelsArray* outModels,
        ModelEndianEnum endian, b32 dumpModelsToConsole) {


    ModelType modelType = ModelType_OBJ;

    ModelParserContext parserContext;
    parserContext.textCurr = dataIn;
    parserContext.textEnd = dataIn + sizeIn;
    parserContext.valueStart = NULL;
    parserContext.valueEnd = NULL;
    parserContext.line = 1;
    parserContext.column = 0;
    parserContext.memIO = memOutput;
    parserContext.token = ModelParserToken_MODEL_BEGIN;
    parserContext.endian = endian;
    parserContext.result.error = NULL;
    parserContext.result.staticError = TRUE;
    parserContext.result.modelIndex = 0;
    parserContext.result.errorLine = 0;
    parserContext.result.errorColumn = 0;

    ModelOffsetsArray modelOffsets;
    MArrayInit(modelOffsets);

    if (dumpModelsToConsole) {
        MLog("Dump:");
    }

    while (parserContext.token != ModelParserToken_END) {
        i32 r = CompileModelWithContext(&parserContext, memOutput);

        if (r) {
            if (r == -2) {
                BuildErrorStringWithValue(&parserContext);
            }
            parserContext.result.errorLine = parserContext.line;
            parserContext.result.errorColumn = parserContext.column;
            return parserContext.result;
        } else {
            if (dumpModelsToConsole) {
                ModelData* modelData = ((ModelData*)(memOutput->mem + parserContext.result.modelStartOffset));
                DecompileModelToConsole(modelData, parserContext.result.modelIndex, modelType);
            }
            ModelOffset* modelOffset = MArrayAddPtr(modelOffsets);
            modelOffset->modelIndex =parserContext.result.modelIndex;
            modelOffset->offset= parserContext.result.modelStartOffset;
        }

        NextTokenSkipNewLines(&parserContext);
    }

    for (int i = 0; i < MArraySize(modelOffsets); ++i) {
        ModelOffset offset = MArrayGet(modelOffsets, i);
        ModelData* modelData = ((ModelData*)(memOutput->mem + offset.offset));
        MArraySet(*outModels, offset.modelIndex, modelData);
    }

    MArrayFree(modelOffsets);

    return parserContext.result;
}

i32 WriteModels(const char* filename, ModelsArray* models, MMemIO* modelsMem) {
    // Write to binary format
    MMemIO offsetsMem;
    u32 modelsStart = (MArraySize(*models) + 1) * 4;
    MMemInitAlloc(&offsetsMem, modelsStart + 4);
    MMemWriteU32BE(&offsetsMem, MArraySize(*models));
    for (int i = 0; i < MArraySize(*models); i++) {
        ModelData* modelData =  models->arr[i];
        u32 offset = 0;
        if (modelData) {
            offset = modelsStart + ((u8*)modelData - modelsMem->mem);
        }
        MMemWriteU32BE(&offsetsMem, offset);
    }
    MMemWriteU32BE(&offsetsMem, 0);

    MFile fileOut = MFileWriteOpen(filename);
    MFileWriteMem(&fileOut, &offsetsMem);
    MFileWriteMem(&fileOut, modelsMem);
    MFileClose(&fileOut);

    MMemFree(&offsetsMem);

    return -1;
}

i32 CompileAndWriteModels(const char* modelsFile, const char* outputFile, MMemIO* outModelMem, ModelsArray* outModels) {
    MReadFileRet file = MFileReadFully(modelsFile);
    if (file.size == 0) {
        return -1;
    }

    ModelCompileResult result = CompileMultipleModels((const char*)file.data, file.size, outModelMem, outModels,
            ModelEndian_LITTLE, TRUE);

    if (result.error) {
        MLogf("Got error: %s", result.error);
        MLogf("    at line: %d, column: %d", result.errorLine, result.errorColumn);
        if (!result.staticError) {
            MFree(result.error); result.error = NULL;
        }
        MFree(file.data);
        return -2;
    }

    MLogf("Saving %s...", outputFile);
    WriteModels(outputFile, outModels, outModelMem);

    return 0;
}

void TestModelCompile(MMemIO* memOutput, const char* testData, u32 testDataLen,
                      const char* error, u32 line, u32 column) {

    memOutput->pos = memOutput->mem;

    ModelCompileResult result = CompileModel(testData, testDataLen, memOutput);

    ModelType modelType = ModelType_OBJ;

    if (error) {
        if (!result.error) {
            MLogf("Expected error: %s", error);
        } else if (MStrCmp(error, result.error) != 0) {
            MLogf("Expecting error: %s\nActual error: %s", error, result.error);
            if (line != result.errorLine) {
                MLogf("Expecting error at line: %d, but was at: %d", line, result.errorLine);
            }
            if (column != result.errorColumn) {
                MLogf("Expecting error at column: %d, but was at: %d", column, result.errorColumn);
            }
        }
    } else {
        if (result.error) {
            MLogf("Got error: %s", result.error);
        }

        ModelData* modelData = ((ModelData*)(memOutput->mem + result.modelStartOffset));
        DecompileModelToConsole(modelData, result.modelIndex, modelType);
    }

    if (result.error) {
        if (!result.staticError) {
            MFree(result.error); result.error = NULL;
        }
    }
}

void TestModelCompileStr(MMemIO* memOutput, const char* testData, const char* error, u32 line, u32 column) {
    return TestModelCompile(memOutput, testData, strlen(testData), error, line, column);
}

void ModelCompile_Test() {
    MMemIO memOutput;
    MMemInitAlloc(&memOutput, 2000);

    ModelData* model = (ModelData*)memOutput.mem;
/*
    TestModelCompileStr(&memOutput,
        "model:\n"
        "\n"
        "vertices:\n"
        "0, 0, 0\n"
        "\n"
        "code:\n",
        NULL, 0, 0
    );

    TestModelCompileStr(&memOutput,
        "model: 1\n"
        "\n"
        "vertices:\n"
        "0, 0, 0\n"
        "\n"
        "code:\n",
        NULL,0,0
    );

    TestModelCompileStr(&memOutput,
        "model:\n"
        "code:\n",
        "Syntax error: unknown model param 'code'", 2, 1
    );

    TestModelCompileStr(&memOutput,
        "code:\n",
        "Syntax error: Expecting 'model:'", 1, 1
    );
*/
//    MReadFileRet file = MFileReadFully("test/grave.txt");
//    if (file.size) {
//        TestModelCompile(&memOutput,
//                         (const char*)file.data,
//                         file.size,
//                         NULL, 0, 0
//        );
//    }
//
//    MFree(memOutput.mem);

//    exit(1);
}
