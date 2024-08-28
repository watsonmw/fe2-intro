#include "drawinfo.h"

void DumpZtree(RasterContext* raster, DebugDrawInfo& drawInfo, RasterOpNode* drawNode, u8* zTreeMemM, MStrWriter* writer);

int DumpDrawFunc(RasterContext* raster, DebugDrawInfo &drawInfo, DrawFunc *drawFunc, MStrWriter* writer) {
    switch (drawFunc->func) {
        case DRAW_FUNC_SPANS_START:
            writer->append("SPANS_START\n");
            return 0;
        case DRAW_FUNC_SPANS_BEZIER: {
            DrawParamsBezier *params = (DrawParamsBezier *) (&drawFunc->params);

            writer->appendf("SPANS_BEZIER ([%d, %d], [%d, %d], [%d, %d], [%d, %d])\n",
                    params->pts[0].x, params->pts[0].y, params->pts[1].x, params->pts[1].y,
                    params->pts[2].x, params->pts[2].y, params->pts[3].x, params->pts[3].y);

            return sizeof(DrawParamsBezier);
        }
        case DRAW_FUNC_SPANS_LINE: {
            DrawParamsLine *params = (DrawParamsLine *) (&drawFunc->params);

            writer->appendf("SPANS_LINE ([%d, %d], [%d, %d])\n",
                  params->x1, params->y1, params->x2, params->y2);

            return sizeof(DrawParamsLine);
        }
        case DRAW_FUNC_SPANS_LINE_CONT: {
            DrawParamsPoint *param2 = (DrawParamsPoint *) (&drawFunc->params);

            writer->appendf("SPANS_LINE_CONT ([%d, %d])\n",
                  param2->x, param2->y);

            return sizeof(DrawParamsPoint);
        }
        case DRAW_FUNC_SPANS_POINT: {
            DrawParamsPoint *param = (DrawParamsPoint *) (&drawFunc->params);

            writer->appendf("SPANS_POINT ([%d, %d])\n",
                           param->x, param->y);

            return sizeof(DrawParamsPoint);
        }
        case DRAW_FUNC_SPANS_DRAW: {
            DrawParamsColour *params = (DrawParamsColour *) (&drawFunc->params);

            writer->appendf("SPANS_DRAW (%d)\n", params->colour);

            return sizeof(DrawParamsColour);
        }
        case DRAW_FUNC_SPANS_END: {
            DrawParamsColour *params = (DrawParamsColour *) (&drawFunc->params);

            writer->appendf("SPANS_END (%d)\n", params->colour);

            return sizeof(DrawParamsColour);
        }
        case DRAW_FUNC_BATCH_END1: {
            writer->appendf("BATCH_END1\n");
            return -1;
        }
        case DRAW_FUNC_BATCH_END: {
            writer->appendf("BATCH_END\n");
            return -2;
        }
        case DRAW_FUNC_NULL: {
            writer->appendf("NULL\n");
            return -3;
        }
        case DRAW_FUNC_NOP: {
            writer->appendf("NOP\n");
            return 0;
        }
        case DRAW_FUNC_LINE: {
            DrawParamsLineColour* params = (DrawParamsLineColour*)(&drawFunc->params);

            writer->appendf("LINE ([%d, %d], [%d, %d], %d)\n",
                           params->x1, params->y1, params->x2, params->y2, params->colour);

            return sizeof(DrawParamsLineColour);
        }
        case DRAW_FUNC_TRI: {
            DrawParamsTri* params = (DrawParamsTri*)(&drawFunc->params);

            writer->appendf("TRI ([%d, %d], [%d, %d], [%d, %d], %d)\n",
                  params->points[0].x, params->points[0].y,
                  params->points[1].x, params->points[1].y,
                  params->points[2].x, params->points[2].y,
                  params->colour);

            return sizeof(DrawParamsTri);
        }
        case DRAW_FUNC_QUAD: {
            DrawParamsQuad* params = (DrawParamsQuad*)(&drawFunc->params);

            writer->appendf("QUAD ([%d, %d], [%d, %d], [%d, %d], [%d, %d], %d)\n",
                            params->points[0].x, params->points[0].y,
                            params->points[1].x, params->points[1].y,
                            params->points[2].x, params->points[2].y,
                            params->points[3].x, params->points[3].y,
                            params->colour);

            return sizeof(DrawParamsQuad);
        }
        case DRAW_FUNC_BEZIER_LINE: {
            DrawParamsBezierColour* params = (DrawParamsBezierColour*)(&drawFunc->params);

            writer->appendf("BEZIER_LINE ([%d, %d], [%d, %d], [%d, %d], [%d, %d], %d)\n",
                           params->pts[0].x, params->pts[0].y, params->pts[1].x, params->pts[1].y,
                           params->pts[2].x, params->pts[2].y, params->pts[3].x, params->pts[3].y, params->colour);

            return sizeof(DrawParamsBezierColour);
        }
        case DRAW_FUNC_FLARE: {
            DrawParamsFlare* params = (DrawParamsFlare*)(&drawFunc->params);

            writer->appendf("HIGHLIGHT ([%d, %d], %d, %d, %d)\n",
                            params->x, params->y, params->diameter, params->innerColour, params->outerColour);

            return sizeof(DrawParamsFlare);
        }
        case DRAW_FUNC_CIRCLE: {
            DrawParamsCircle* params = (DrawParamsCircle*)(&drawFunc->params);

            writer->appendf("CIRCLE ([%d, %d], %d, %d)\n",
                            params->x, params->y, params->diameter, params->colour);

            return sizeof(DrawParamsCircle);
        }
        case DRAW_FUNC_RINGED_CIRCLE: {
            DrawParamsRingedCircle* params = (DrawParamsRingedCircle*)(&drawFunc->params);

            writer->appendf("RINGED_CIRCLE (%d, [%d, %d], , %d, %d)\n",
                            params->innerColour, params->x, params->y, params->diameter, params->outerColour);

            return sizeof(DrawParamsRingedCircle);
        }
        case DRAW_FUNC_SUBTREE: {
             writer->appendf("SUBTREE_START\n");
            RasterOpNode* params = (RasterOpNode*)(&drawFunc->params);

            DumpZtree(raster, drawInfo, params, raster->depthTree.data, writer);

#ifdef FINTRO_INSPECTOR
            writer->appendf("       ");
#endif
            writer->appendf("          SUBTREE_END\n");
            return 0;
        }
        case DRAW_FUNC_BODY_START: {
            writer->appendf("BODY_START\n");
            return 0;
        }
        case DRAW_FUNC_BODY_LINE: {
            DrawParamsLineColour *params = (DrawParamsLineColour *) (&drawFunc->params);

            writer->appendf("BODY_LINE ([%d, %d], [%d, %d], %d)\n",
                           params->x1, params->y1, params->x2, params->y2,
                           params->colour);

            return sizeof(DrawParamsLineColour);
        }
        case DRAW_FUNC_BODY_BEZIER: {
            DrawParamsBezierColour* params = (DrawParamsBezierColour*)(&drawFunc->params);
            writer->appendf("BODY_BEZIER ([%d, %d], [%d, %d], [%d, %d], [%d, %d], %d)\n",
                            params->pts[0].x, params->pts[0].y, params->pts[1].x, params->pts[1].y,
                            params->pts[2].x, params->pts[2].y, params->pts[3].x, params->pts[3].y, params->colour);

            return sizeof(DrawParamsBezierColour);
        }
        case DRAW_FUNC_BODY_TOGGLE_COLOUR: {
            DrawParamsBodyToggleColour* params = (DrawParamsBodyToggleColour*)(&drawFunc->params);
            writer->appendf("BODY_TOGGLE_COLOUR (%d, %d)\n", params->offset, params->colour);
            return sizeof(DrawParamsBodyToggleColour);
        }
        case DRAW_FUNC_BODY_DRAW_1: {
            DrawParamsColour8* params = (DrawParamsColour8*)(&drawFunc->params);
            writer->appendf("BODY_DRAW_1 ([%d, %d, %d, %d, %d, %d, %d, %d])\n",
                           params->colour[0], params->colour[1], params->colour[2], params->colour[3],
                           params->colour[4], params->colour[5], params->colour[6], params->colour[7]);

            return sizeof(DrawParamsColour8);
        }
        case DRAW_FUNC_BODY_DRAW_2: {
            DrawParamsColour16* params = (DrawParamsColour16*)(&drawFunc->params);
            writer->appendf("BODY_DRAW_2 ([%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d])\n",
                           params->colour[0], params->colour[1], params->colour[2], params->colour[3],
                           params->colour[4], params->colour[5], params->colour[6], params->colour[7],
                           params->colour[8], params->colour[9], params->colour[10], params->colour[11],
                           params->colour[12], params->colour[13], params->colour[14], params->colour[15]);

            return sizeof(DrawParamsColour16);
        }
        case DRAW_FUNC_BODY_DRAW_3: {
            DrawParamsColour8* params = (DrawParamsColour8*)(&drawFunc->params);
            writer->appendf("BODY_DRAW_3 ([%d, %d, %d, %d, %d, %d, %d, %d])\n",
                           params->colour[0], params->colour[1], params->colour[2], params->colour[3],
                           params->colour[4], params->colour[5], params->colour[6], params->colour[7]);

            return sizeof(DrawParamsColour8);
        }
        case DRAW_FUNC_TEXT: {
            DrawParamsText* params = (DrawParamsText*)(&drawFunc->params);
            writer->appendf("TEXT (%d, [%d, %d], %d, %d)\n",
                           params->strCode, params->x, params->y, params->strOffset, params->t3);
           return sizeof(DrawParamsText);
        }
        case DRAW_FUNC_CIRCLES: {
            if (raster->legacy) {
                DrawParamsLegacyCircles* params = (DrawParamsLegacyCircles*)(&drawFunc->params);

                writer->appendf("CIRCLES (%d, %d", params->width, params->colour);

                int i = 0;
                while (params->pos[i] != 0xffff) {
                    i16 x = params->pos[i++];
                    i16 y = params->pos[i++];
                    writer->appendf(", [%d, %d]", x, y);
                }

                writer->append(")\n");
                return sizeof(DrawParamsLegacyCircles) + ((i + 1) * 2);
            } else {
                DrawParamsCircles* params = (DrawParamsCircles*)(&drawFunc->params);

                writer->appendf("CIRCLES (%d, %d", params->width, params->colour);

                for (int i = 0; i < params->num;) {
                    i16 x = params->pos[i++];
                    i16 y = params->pos[i++];
                    writer->appendf(", [%d, %d]", x, y);
                }

                writer->append(")\n");
                return sizeof(DrawParamsCircles) + (params->num * 2);
            }
        }
        default:
            writer->appendf("ERROR Unknown draw type %4hx\n", drawFunc->func);
            return -1;
    }
}

int DumpDrawBatch(RasterContext* raster, DebugDrawInfo &drawInfo, RasterOpNode *drawNode, MStrWriter* writer) {
    u8* currentFunc = (u8*)&drawNode->func;

    DrawFunc* drawFunc = (DrawFunc*)(currentFunc);

    // Draw batch
    int size = 0;
    do {
        currentFunc += sizeof(drawFunc->func);
        currentFunc += size;
        drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINTRO_INSPECTOR
        u16 offset1 = (u8*)drawFunc - (u8*)raster->depthTree.data;
        u32 insOffsetTmp = raster->depthTree.insOffset[offset1];
        writer->appendf("%05x | ", insOffsetTmp);
#endif
        writer->append("          ");
        size = DumpDrawFunc(raster, drawInfo, drawFunc, writer);
    } while (size >= 0);

    drawInfo.batchesDrawn++;

    return 0;
}

int DumpBodySpanFunc(RasterContext* raster, DebugDrawInfo &drawInfo, RasterOpNode *drawNode, MStrWriter* writer) {
    u8* currentFunc = (u8*)&drawNode->func;

    DrawFunc* drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINTRO_INSPECTOR
    u16 offset1 = (u8*)drawFunc - (u8*)raster->depthTree.data;
    u32 insOffsetTmp = raster->depthTree.insOffset[offset1];
    writer->appendf("%05x | ", insOffsetTmp);
#endif

    int size = 0;
    bool first = true;
    while (drawFunc->func != DRAW_FUNC_BODY_DRAW_1 &&
           drawFunc->func != DRAW_FUNC_BODY_DRAW_2 &&
           drawFunc->func != DRAW_FUNC_BODY_DRAW_3) {

        if (first) {
            first = false;
        } else {
            writer->append("          ");
        }
        size = DumpDrawFunc(raster, drawInfo, drawFunc, writer);
        currentFunc += sizeof(drawFunc->func);
        currentFunc += size;
        drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINTRO_INSPECTOR
        u16 offset2 = (u8*)drawFunc - (u8*)raster->depthTree.data;
        u32 insOffsetTmp2 = raster->depthTree.insOffset[offset2];
        writer->appendf("%05x | ", insOffsetTmp2);
#endif
    }

    if (!first) {
        writer->append("          ");
    }
    DumpDrawFunc(raster, drawInfo, drawFunc, writer);

    return size;
}

int DumpSpanFunc(RasterContext* raster, DebugDrawInfo &drawInfo, RasterOpNode *drawNode, MStrWriter* writer) {
    u8* currentFunc = (u8*)&drawNode->func;

    DrawFunc* drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINTRO_INSPECTOR
    u16 offset1 = (u8*)drawFunc - (u8*)raster->depthTree.data;
    u32 insOffsetTmp = raster->depthTree.insOffset[offset1];
    writer->appendf("%05x | ", insOffsetTmp);
#endif

    int size = 0;
    bool first = true;
    while (drawFunc->func != DRAW_FUNC_SPANS_END) {
        if (first) {
            first = false;
        } else {
            writer->append("          ");
        }
        size = DumpDrawFunc(raster, drawInfo, drawFunc, writer);
        currentFunc += sizeof(drawFunc->func);
        currentFunc += size;
        drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINTRO_INSPECTOR
        u16 offset2 = (u8*)drawFunc - (u8*)raster->depthTree.data;
        u32 insOffsetTmp2 = raster->depthTree.insOffset[offset2];
        writer->appendf("%05x | ", insOffsetTmp2);
#endif
    }

    if (!first) {
        writer->append("          ");
    }
    DumpDrawFunc(raster, drawInfo, drawFunc, writer);

    return size;
}

int DumpDrawNodeInfo(RasterContext* raster, DebugDrawInfo &drawInfo, RasterOpNode *renderNode, MStrWriter* writer) {
    DrawFunc* drawFunc = &renderNode->func;

    switch (drawFunc->func) {
        case DRAW_FUNC_BATCH_START:
            writer->append("BATCH_START\n");
            DumpDrawBatch(raster, drawInfo, renderNode, writer);
            break;
        case DRAW_FUNC_SPANS_START:
        case DRAW_FUNC_SPANS_LINE_CONT:
        case DRAW_FUNC_SPANS_LINE:
        case DRAW_FUNC_SPANS_POINT:
        case DRAW_FUNC_SPANS_END:
        case DRAW_FUNC_SPANS_BEZIER:
        case DRAW_FUNC_SPANS_DRAW:
            DumpSpanFunc(raster, drawInfo, renderNode, writer);
            break;
        case DRAW_FUNC_BODY_START:
        case DRAW_FUNC_BODY_BEZIER:
        case DRAW_FUNC_BODY_LINE:
        case DRAW_FUNC_BODY_TOGGLE_COLOUR:
        case DRAW_FUNC_BODY_DRAW_1:
        case DRAW_FUNC_BODY_DRAW_2:
        case DRAW_FUNC_BODY_DRAW_3:
            DumpBodySpanFunc(raster, drawInfo, renderNode, writer);
            break;
        case DRAW_FUNC_LINE:
        case DRAW_FUNC_TRI:
        case DRAW_FUNC_QUAD:
        case DRAW_FUNC_BEZIER_LINE:
        case DRAW_FUNC_FLARE:
        case DRAW_FUNC_CIRCLE:
        case DRAW_FUNC_RINGED_CIRCLE:
        case DRAW_FUNC_CIRCLES:
        case DRAW_FUNC_TEXT:
        case DRAW_FUNC_SUBTREE:
            DumpDrawFunc(raster, drawInfo, drawFunc, writer);
            break;
        case DRAW_FUNC_NULL:
            writer->append("NULL\n");
            break;

        default:
            writer->appendf("ERROR Unknown draw type %4hx\n", drawFunc->func);
            return -1;
    }

    return 0;
}

void DumpZtree(RasterContext* raster, DebugDrawInfo& drawInfo, RasterOpNode* drawNode, u8* zTreeMem, MStrWriter* writer) {
    MPtrArray<RasterOpNode> stack(32);

    while (drawNode != NULL || stack.size()) {
        while (drawNode != NULL) {
            stack.add(drawNode);
            if (drawNode->rightOffset) {
                drawNode = (RasterOpNode*)(zTreeMem + drawNode->rightOffset);
            } else {
                drawNode = NULL;
            }
        }

        drawNode = stack.pop();

#ifdef FINTRO_INSPECTOR
        u16 offset1 = (u8*)drawNode - (u8*)raster->depthTree.data;
        u32 insOffsetTmp = raster->depthTree.insOffset[offset1];
        writer->appendf("%05x | ", insOffsetTmp);
#endif

        writer->appendf("%8x: ", drawNode->z);

        if (DumpDrawNodeInfo(raster, drawInfo, drawNode, writer) < 0) {
            break;
        }

        if (drawNode->leftOffset) {
            drawNode = (RasterOpNode*)(zTreeMem + drawNode->leftOffset);
        } else {
            drawNode = NULL;
        }
    }
}

void DumpDrawInfo(RasterContext* raster, DebugDrawInfo& drawInfo, MStrWriter* writer) {
    drawInfo.batchesDrawn = 0;

    u8* zTreeMem = raster->depthTree.data;

    RasterOpNode* drawNode = (RasterOpNode*)(zTreeMem + raster->depthTree.firstNodeOffset);

    DumpZtree(raster, drawInfo, drawNode, (u8*) zTreeMem, writer);
}

void DumpPaletteInfo(RasterContext* raster, RGB* screenPalette, DebugPaletteInfo& paletteInfo) {
    PaletteContext* paletteContext = &raster->paletteContext;

    MStrWriter* writer = &paletteInfo.writer;
    for (int i = 0; i < 256; i++) {
        VirtualPalette* palette = paletteContext->virtualPalette + i;
        RGB* screenColour = screenPalette + palette->index;
        u32 screenColHex = (screenColour->r << 16) + (screenColour->g << 8) + screenColour->b;
        writer->appendf("%03d: %04hx %02hhx %02hhx %06x\n", i, palette->colourOf4096, palette->index, palette->state,
                       screenColHex);
    }
}
