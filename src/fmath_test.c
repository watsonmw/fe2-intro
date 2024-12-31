#include "fmath.c"
#include "mtest.h"


#define FASSERT_FLOAT(f, expectedV, expectedP) do { \
    m_total_tests++; \
    if ((f).v == (expectedV) && (f).p == (expectedP)) { \
        m_passed_tests++; \
        MLogf(MANSI_COLOR_GREEN "PASS" MANSI_COLOR_RESET ": %s:%d - Expected %x:%d, got %x:%d", \
              __FILE__, __LINE__, (expectedV), (expectedP), (f).v, (f).p); \
    } else { \
        MLogf(MANSI_COLOR_RED "FAIL" MANSI_COLOR_RESET ": %s:%d - Expected %x:%d, got %x:%d", \
              __FILE__, __LINE__, (expectedV), (expectedP), (f).v, (f).p); \
    } \
} while(0)


void test_makeint_float16() {
    Float16 f16;

    f16 = Float16MakeFromInt(1);
    FASSERT_FLOAT(f16, 0x4000, 1);

    f16 = Float16MakeFromInt(2);
    FASSERT_FLOAT(f16, 0x4000, 2);

    f16 = Float16MakeFromInt(-1);
    FASSERT_FLOAT(f16, -0x8000, 0);

    f16 = Float16MakeFromInt(-2);
    FASSERT_FLOAT(f16, -0x8000, 1);
}

void test_makeint_float32() {
    Float32 f32;

    f32 = Float32MakeFromInt(1);
    FASSERT_FLOAT(f32, 0x40000000, 1);

    f32 = Float32MakeFromInt(-1);
    FASSERT_FLOAT(f32, -0x80000000, 0);

    f32 = Float32MakeFromInt(100);
    FASSERT_FLOAT(f32, 0x64000000, 7);
}

void test_rebase16() {
    Float16 f16;
    f16 = Float16MakeFromInt(1);
    f16 = Float16Rebase(f16);
    FASSERT_FLOAT(f16, 0x4000, 1);

    f16.v >>= 1;
    f16 = Float16Rebase(f16);
    FASSERT_FLOAT(f16, 0x4000, 0);

    f16.v >>= 1;
    f16 = Float16Rebase(f16);
    FASSERT_FLOAT(f16, 0x4000, -1);
}

void test_rebase32() {
    Float32 f32;
    f32 = Float32MakeFromInt(1);
    f32 = Float32Rebase(f32);
    FASSERT_FLOAT(f32, 0x40000000, 1);

    f32.v >>= 1;
    f32 = Float32Rebase(f32);
    FASSERT_FLOAT(f32, 0x40000000, 0);

    f32.v >>= 1;
    f32 = Float32Rebase(f32);
    FASSERT_FLOAT(f32, 0x40000000, -1);
}

void test_add16() {
    Float16 f1, f2, f3;
    f1 = Float16MakeFromInt(-1);
    f2 = Float16MakeFromInt(-1);
    f3 = Float16Add(f1, f2);
    FASSERT_FLOAT(f3, -0x8000, 1);

    f1 = Float16MakeFromInt(-2);
    f2 = Float16MakeFromInt(-2);
    f3 = Float16Add(f1, f2);
    FASSERT_FLOAT(f3, -0x8000, 2);

    f1 = Float16MakeFromInt(2);
    f2 = Float16MakeFromInt(-2);
    f3 = Float16Add(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);
}

void test_sub16() {
    Float16 f1, f2, f3;
    f1 = Float16MakeFromInt(-1);
    f2 = Float16MakeFromInt(1);
    f3 = Float16Sub(f1, f2);
    FASSERT_FLOAT(f3, -0x8000, 1);

    f1 = Float16MakeFromInt(-2);
    f2 = Float16MakeFromInt(2);
    f3 = Float16Sub(f1, f2);
    FASSERT_FLOAT(f3, -0x8000, 2);

    f1 = Float16MakeFromInt(2);
    f2 = Float16MakeFromInt(2);
    f3 = Float16Sub(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);
}

void test_mult16() {
    Float16 f1, f2, f3, r;
    f1 = Float16MakeFromInt(-1);
    f2 = Float16MakeFromInt(-1);
    f3 = Float16Mult(f1, f2);
    FASSERT_FLOAT(f3, 0x4000, 1);

    r = Float16Extract(f3);
    FASSERT_FLOAT(r, 1, 14);

    f1 = Float16MakeFromInt(-2);
    f2 = Float16MakeFromInt(-4);
    f3 = Float16Mult(f1, f2);
    FASSERT_FLOAT(f3, 0x4000, 4);

    r = Float16Extract(f3);
    FASSERT_FLOAT(r, 8, 11);

    f1 = Float16MakeFromInt(2);
    f2 = Float16MakeFromInt(-4);
    f3 = Float16Mult(f1, f2);
    FASSERT_FLOAT(f3, -0x4000, 4);

    r = Float16Extract(f3);
    FASSERT_FLOAT(r, -8, 11);
}

void test_add32() {
    Float32 f1, f2, f3;
    f1 = Float32MakeFromInt(-1);
    f2 = Float32MakeFromInt(-1);
    f3 = Float32Add(f1, f2);
    FASSERT_FLOAT(f3, 0x80000000, 1);

    f1 = Float32MakeFromInt(-1);
    f2 = Float32MakeFromInt(1);
    f3 = Float32Add(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);

    f1 = Float32MakeFromInt(1);
    f2 = Float32MakeFromInt(1);
    f3 = Float32Add(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 2);

    f1 = Float32MakeFromInt(1);
    f2 = Float32MakeFromInt(0);
    f3 = Float32Add(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 1);

    f1 = Float32MakeFromInt(0);
    f2 = Float32MakeFromInt(1);
    f3 = Float32Add(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 1);

    f1 = Float32MakeFromInt(0);
    f2 = Float32MakeFromInt(0);
    f3 = Float32Add(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);

    f1 = Float32MakeFromInt(-2);
    f2 = Float32MakeFromInt(-2);
    f3 = Float32Add(f1, f2);
    FASSERT_FLOAT(f3, 0x80000000, 2);

    f1 = Float32MakeFromInt(2);
    f2 = Float32MakeFromInt(-2);
    f3 = Float32Add(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);
}

void test_sub32() {
    Float32 f1, f2, f3;
    f1 = Float32MakeFromInt(-1);
    f2 = Float32MakeFromInt(1);
    f3 = Float32Sub(f1, f2);
    FASSERT_FLOAT(f3, 0x80000000, 1);

    f1 = Float32MakeFromInt(-1);
    f2 = Float32MakeFromInt(-1);
    f3 = Float32Sub(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);

    f1 = Float32MakeFromInt(1);
    f2 = Float32MakeFromInt(-1);
    f3 = Float32Sub(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 2);

    f1 = Float32MakeFromInt(1);
    f2 = Float32MakeFromInt(0);
    f3 = Float32Sub(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 1);

    f1 = Float32MakeFromInt(0);
    f2 = Float32MakeFromInt(-1);
    f3 = Float32Sub(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 1);

    f1 = Float32MakeFromInt(0);
    f2 = Float32MakeFromInt(0);
    f3 = Float32Sub(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);

    f1 = Float32MakeFromInt(-2);
    f2 = Float32MakeFromInt(2);
    f3 = Float32Sub(f1, f2);
    FASSERT_FLOAT(f3, 0x80000000, 2);

    f1 = Float32MakeFromInt(2);
    f2 = Float32MakeFromInt(2);
    f3 = Float32Sub(f1, f2);
    MASSERT_INT_EQ(f3.v, 0);
}

void test_mult32() {
    Float16 f1, f2, r1;
    Float32 f3;
    f1 = Float16MakeFromInt(-1);
    f2 = Float16MakeFromInt(-1);
    f3 = Float16Mult32(f1, f2);
    FASSERT_FLOAT(f3, 0x40000000, 1);

    r1 = Float32Convert16(f3);
    FASSERT_FLOAT(r1, 0x4000, 1);
}

void test_scale_below() {
    Vec3i32 v = {0, 0, 0x3f99};
    i8 scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 0);

    v[0] = 0x0; v[1] = 0; v[2] = 0x4000;
    scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 1);

    v[2] = 0x8000;
    scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 2);

    v[0] = 0x8000;
    scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 2);

    v[1] = 0x8000;
    scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 2);

    v[0] = 0x0; v[1] = 0; v[2] = -0x3fff;
    scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 0);

    v[0] = 0x0; v[1] = 0; v[2] = -0x40000000;
    scale = GetScaleBelow0x4000(v);
    MASSERT_INT_EQ(scale, 17);
}

int main(int argc, char** argv) {
    MTEST_FUNC(test_makeint_float16());
    MTEST_FUNC(test_makeint_float32());
    MTEST_FUNC(test_rebase16());
    MTEST_FUNC(test_rebase32());
    MTEST_FUNC(test_add16());
    MTEST_FUNC(test_sub16());
    MTEST_FUNC(test_mult16());
    MTEST_FUNC(test_sub32());
    MTEST_FUNC(test_mult32());
    MTEST_FUNC(test_scale_below());
    MTEST_PRINT_RESULTS();

    return 0;
}
