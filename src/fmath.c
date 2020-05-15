#include "fmath.h"

#include "math.h"
#include "mlib.h"

i16 FMath_sine[4096];
i16 FMath_arccos[128];

void FMath_BuildLookupTables() {
    int size = sizeof(FMath_sine) / sizeof(i16);
    double d = 2.0 * M_PI / (double)size;
    for (int i = 0; i <  size; i++) {
        double v = sin(i * d);
        if (v < 0) {
            v = 1 - v;
        }
        FMath_sine[i] = (i16)(v * 0x7fff);
    }

    int asize = sizeof(FMath_arccos) / sizeof(u16);
    double ad = (double) asize / 2;
    for (int i = 0; i <  asize / 2; i++) {
        double vin = i / ad;
        double v = acos(vin);
        i32 s = (i32)((v / M_PI_2) * 0x7fff);
        FMath_arccos[i] = (i16)s;
    }

    for (int i = 1; i <= (asize / 2); i++) {
        double vin = -i / ad;
        double v = acos(vin);
        i32 s = (i32)((v / M_PI_2) * 0x7fff);
        FMath_arccos[asize - i] = (i16)s;
    }
}

void testSqrt(u32 val, u32 expected) {
    u32 result1 = FMath_SqrtFunc16x8(val) >> 8;
    u32 result2 = FMath_SqrtFunc32(val);

    if (result1 != expected) {
        MLogf("SqrtFunc16x8 %d was: %d, expected: %d", val, result1, expected);
    }

    if (result2 != expected) {
        MLogf("SqrtFunc %d was: %d, expected: %d", val, result2, expected);
    }
}

void testSqrts(void) {
    testSqrt(1, 1);
    testSqrt(4, 2);
    testSqrt(9, 3);
    testSqrt(25, 5);
    testSqrt(100, 10);
    testSqrt(10000, 100);
    testSqrt(1000000, 1000);
    testSqrt(100000000, 10000);
    testSqrt(900000000, 30000);
    testSqrt(1600000000, 40000);
//    testSqrt(1639359121, 40489); // known issue SqrtFunc16x8 fails this
}

void testFloats(void) {
//    Float32 v1{0x40000000,0}; // 5.0e-1 | 0.5
//    Float32 v2{0x40000000,1}; // 5.0e-1 | 0.5
//    Float32 v3{0x40000000,2}; // 5.0e-1 | 0.5
//    MLogf("%g %g %g", Float32ieee(v1), Float32ieee(v2), Float32ieee(v3));
//
//    Float32 f{0x40000000,1}; // 5.0e-1 | 0.5
//    Float32 f2 = Float32Add(f, f);
//
//    char d[200];
//    Float32Print(f, d, 200);
//    testSqrts();
//
//    MLogf("%g - %g = %g", Float32ieee(f), Float32ieee(f), Float32ieee(Float32Sub(f, f)));
//    MLogf("%g + %g = %g", Float32ieee(f), Float32ieee(f), Float32ieee(f2));
//    MLogf("sqrt(%g) = %f", Float32ieee(f), Float16ieee(Float32Sqrt(f)));
//
//    Float16 d1{0x4000,0};
//    Float16 d2{0x4000,1};
//    Float16 d3{0x4000,2};
//    MLogf("%f %f %f", Float16ieee(d1), Float16ieee(d2), Float16ieee(d3));
//    MLogf("%f %f %f",
//          Float16ieee(Float16Sqrt(d1)),
//          Float16ieee(Float16Sqrt(d2)),
//          Float16ieee(Float16Sqrt(d3)));
//
//    MLogf("%f", Float16ieee(Float16Make(-2)));
//
//    MLogf("%f", Float16ieee(Float16Make(-1)));
//
//    MLogf("%f", Float16ieee(Float16Make(1)));
//    MLogf("%f", Float16ieee(Float16Make(2)));
//    MLogf("%f", Float16ieee(Float16Make(3)));
//    MLogf("%f", Float16ieee(Float16Make(4)));
//    MLogf("%f", Float16ieee(Float16Make(4000)));
//    MLogf("%f", Float16ieee(Float16Make(100000)));
//    MLogf("%f", Float16ieee(Float16Make(0)));
//    MLogf("%f", Float16ieee(Float16Make(-3)));
//    MLogf("%f", Float16ieee(Float16Make(-4)));
//    MLogf("%f", Float16ieee(Float16Make(-5)));
//    MLogf("%f", Float16ieee(Float16Make(-6)));
//    MLogf("%f", Float16ieee(Float16Make(-1000000)));
//
//    Float16 minus1 = Float16Make(-1);
//
//    MLogf("%f", Float16ieee(Float16Add(minus1, minus1)));
//
//    for (int i = -8; i < 9; ++i) {
//        Float16 f = Float16Make(i);
//        Float16 extract = Float16Extract(f);
//        MLogf("%d (%x, %x) : %d : %f", i, f.v, f.p, extract.v, Float16ieee(f));
//    }
//
//    Float16 one = Float16Make(1);
//    Float16 three = Float16Make(3);
//    Float16 r = Float16Div(one, three);
//
//    MLogf("%f/%f = %f", Float16ieee(one), Float16ieee(three), Float16ieee(r));
//
//    Float32 z2 = {0x4bbd1cc8,0x1c};
//    Float32 r2 = {0x4ef51080, 0x3c};
//
//    Float32 tangentZ2 = Float32Sub(z2, r2); // tangent len ignoring 2d x,y pos
//
//    MLogf("%g = %g - %g", Float32ieee(tangentZ2), Float32ieee(z2), Float32ieee(r2));
//
//
//    Float16 v1 = {0x7800,0x0};
//    Float16 v2 = {0x5a50, 0x1b};
//
//    Float16 v3 = Float16Div(v1, v2);
//
//    MLogf("%g = %g / %g", Float16ieee(v3), Float16ieee(v1), Float16ieee(v2));
}

void FMath_Test() {
    testSqrts();
    testFloats();

    u16 angle = 0x4000;
    i16 sine;
    i16 cosine;

    FMath_BuildLookupTables();

    LookupSineAndCosine(angle, &sine, &cosine);
}

