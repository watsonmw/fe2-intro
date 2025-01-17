//
// Reverse Engineered from:
//   [Frontier: Elite II © David Braben 1993 & 1994](https://en.wikipedia.org/wiki/Frontier:_Elite_II)
//

#ifndef FINTRO_FMATH_H
#define FINTRO_FMATH_H

#include "mlib.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// Some fixed point fraction constants, these are used when calculating Beziers control points for an arc.
#define F16_FOUR_THIRDS 0xaaaa
#define F16_TWO_THIRDS 0x5554

// Fractional bits of frontier floats (this is the 'significand' in ieee floats, also referred to as mantissa)
#define F16_FRAC_BITS 15
#define F32_FRAC_BITS 31

// Matrices are by convention m[row][column]
typedef i16 Matrix3x3i16[3][3];

typedef i16 Vec3i16[3];
typedef i32 Vec3i32[3];

typedef struct sVec2_16 {
    i16 x;
    i16 y;
} Vec2i16;

typedef struct sFloat16 {
    i16 v; // value - fractional part / significand
    i16 p; // precision - power of two exponent
} Float16;

typedef struct sFloat32 {
    i32 v; // value - fractional part / significand
    i16 p; // precision - power of two exponent
} Float32;

extern i16 FMath_sine[4096];
extern i16 FMath_arccos[128];

#ifdef AMIGA
#define FMATH_USE_INLINE_TABLES
#endif

#ifndef FMATH_USE_INLINE_TABLES
void FMath_BuildLookupTables(void);
#endif

// Returns fixed point 16.8
MINLINE u32 FMath_SqrtFunc16x8(u32 num) {
    u32 bit = 1u << 30;
    u32 res = 0;
    while (bit > 0x40) {
        u32 t = res + bit;
        if (num >= t) {
            num -= t;
            res = t + bit; // equivalent to q += 2*bit
        }
        num <<= 1;
        bit >>= 1;
    }
    res >>= 8;
    return res;
}

// Takes an int / returns an int
MINLINE u32 FMath_SqrtFunc32(u32 num) {
    u32 bit = 1u << 30;
    u32 res = 0;

    // "bit" starts at the highest power of four <= the argument.
    while (bit > num) {
        bit >>= 2;
    }

    while (bit != 0) {
        u32 t = res + bit;
        if (num >= t) {
            num -= t;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/*
 * Check 'val' is within the range 0 -> maxValue
 */
MINLINE b32 FMath_RangedCheck(i32 val, u32 maxValue) {
    return (u32)val < maxValue;
}

MINLINE i32 FMath_Distance3i32(i32 x, i32 y, i32 z) {
    return (i32)FMath_SqrtFunc32((u32)((x * x) + (y * y) + (z * z)));
}

MINLINE i32 Vec3i32Length(const Vec3i32 v) {
    return FMath_Distance3i32(v[0], v[1], v[2]);
}

MINLINE u32 Vec3i16Length(const Vec3i16 vec) {
    return FMath_SqrtFunc32(
            ((i32) vec[0] * (i32) vec[0]) + ((i32) vec[1] * (i32) vec[1]) + ((i32) vec[2] * (i32) vec[2]));
}

MINLINE void LookupSineAndCosine(u16 angle, i16* sine, i16* cosine) {
    u16 mask = 1 << 0xf;
    if (angle & mask) {
        u16 lookupAngle = (angle ^ mask) >> 4;
        *sine = (i16)(-FMath_sine[lookupAngle]);
    } else {
        *sine = FMath_sine[angle >> 4];
    }

    angle += 0x4000;
    if (angle & mask) {
        u16 lookupAngle = (angle ^ mask) >> 4;
        *cosine = (i16)(-FMath_sine[lookupAngle]);
    } else {
        *cosine = FMath_sine[angle >> 4];
    }
}

MINLINE void Vec3i32FractMult(const Vec3i32 v, i32 s, Vec3i32 dest) {
    dest[0] = (v[0] * s) >> 15;
    dest[1] = (v[1] * s) >> 15;
    dest[2] = (v[2] * s) >> 15;
}

MINLINE void Vec3i32ScalarMult(const Vec3i32 v, i32 s, Vec3i32 dest) {
    dest[0] = v[0] * s;
    dest[1] = v[1] * s;
    dest[2] = v[2] * s;
}

// Setup identity matrix for 16bit fixed point
// Each entry in the matrix is in the range -1 (0x8000) -> 1 (0x7fff).
MINLINE void Matrix3x3i16Identity(Matrix3x3i16 d) {
    d[0][0] = 0x7fff;
    d[0][1] = 0;
    d[0][2] = 0;

    d[1][0] = 0;
    d[1][1] = 0x7fff;
    d[1][2] = 0;

    d[2][0] = 0;
    d[2][1] = 0;
    d[2][2] = 0x7fff;
}

// d = v * m
//   row vector on the left
MINLINE void MatrixMult_Vec3i32(const Vec3i32 v, const Matrix3x3i16 m, Vec3i32 d) {
    d[0] = (m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2]) >> 15;
    d[1] = (m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2]) >> 15;
    d[2] = (m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2]) >> 15;
}

// d = v * m (row vector on right)
//   row vector on the left
MINLINE void MatrixMult_Vec3i16(const Vec3i16 v, const Matrix3x3i16 m, Vec3i16 d) {
    d[0] = (i16)((m[0][0] * (i32) v[0] + m[1][0] * (i32) v[1] + m[2][0] * (i32) v[2]) >> 15);
    d[1] = (i16)((m[0][1] * (i32) v[0] + m[1][1] * (i32) v[1] + m[2][1] * (i32) v[2]) >> 15);
    d[2] = (i16)((m[0][2] * (i32) v[0] + m[1][2] * (i32) v[1] + m[2][2] * (i32) v[2]) >> 15);
}

// d = v * transpose(m)
//   if m is a rotation, this inverts the rotation
//   row vector on the left
MINLINE void MatrixMult_Vec3i16_T(const Vec3i16 v, const Matrix3x3i16 m, Vec3i16 d) {
    d[0] = (i16)((m[0][0] * (i32) v[0] + m[0][1] * (i32) v[1] + m[0][2] * (i32) v[2]) >> 15);
    d[1] = (i16)((m[1][0] * (i32) v[0] + m[1][1] * (i32) v[1] + m[1][2] * (i32) v[2]) >> 15);
    d[2] = (i16)((m[2][0] * (i32) v[0] + m[2][1] * (i32) v[1] + m[2][2] * (i32) v[2]) >> 15);
}

// d = v * transpose(m)
//   if m is a rotation, this inverts the rotation
//   row vector on the left
MINLINE void MatrixMult_Vec3i32_T(const Vec3i32 v, const Matrix3x3i16 m, Vec3i16 d) {
    d[0] = (i16)((m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2]) >> 15);
    d[1] = (i16)((m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2]) >> 15);
    d[2] = (i16)((m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]) >> 15);
}

MINLINE void Matrix3i16Copy(const Matrix3x3i16 src, Matrix3x3i16 dest) {
    dest[0][0] = src[0][0];
    dest[0][1] = src[0][1];
    dest[0][2] = src[0][2];
    dest[1][0] = src[1][0];
    dest[1][1] = src[1][1];
    dest[1][2] = src[1][2];
    dest[2][0] = src[2][0];
    dest[2][1] = src[2][1];
    dest[2][2] = src[2][2];
}

static u16 sMatrix3x3RotationRowOffsets[] = {
        1, // x
        2, // y
        0, // z
        1
};

// Apply rotation about an axis to given matrix
MINTERNAL void Matrix3x3i16RotateAxis(Matrix3x3i16 matrix, u16 rotationAxis, i16 sine, i16 cosine) {
    u16 row1 = sMatrix3x3RotationRowOffsets[rotationAxis];
    u16 row2 = sMatrix3x3RotationRowOffsets[rotationAxis + 1];

    i32 x1 = matrix[row1][0];
    i32 y1 = matrix[row1][1];
    i32 z1 = matrix[row1][2];

    i32 x2 = matrix[row2][0];
    i32 y2 = matrix[row2][1];
    i32 z2 = matrix[row2][2];

    i32 dx = (((x1 * cosine) + (x2 * sine)) >> (u16)15);
    i32 dy = (((y1 * cosine) + (y2 * sine)) >> (u16)15);
    i32 dz = (((z1 * cosine) + (z2 * sine)) >> (u16)15);

    matrix[row1][0] = (i16)dx;
    matrix[row1][1] = (i16)dy;
    matrix[row1][2] = (i16)dz;

    dx = (((z2 * cosine) - (z1 * sine)) >> (u16)15);
    dy = (((y2 * cosine) - (y1 * sine)) >> (u16)15);
    dz = (((x2 * cosine) - (x1 * sine)) >> (u16)15);

    matrix[row2][2] = (i16)dx;
    matrix[row2][1] = (i16)dy;
    matrix[row2][0] = (i16)dz;
}

// Apply rotation about an axis to given matrix
MINLINE void Matrix3x3i16RotateAxisAngle(Matrix3x3i16 matrix, u16 rotationAxis, u16 angle) {
    i16 sine = 0;
    i16 cosine = 0;
    LookupSineAndCosine(angle, &sine, &cosine);

    Matrix3x3i16RotateAxis(matrix, rotationAxis, sine, cosine);
}

MINLINE void Vec3i16Copy(const Vec3i16 v, Vec3i16 dest) {
    dest[0] = v[0];
    dest[1] = v[1];
    dest[2] = v[2];
}

MINLINE void Vec3i16Copy32(const Vec3i32 v, Vec3i16 dest) {
    dest[0] = (i16)v[0];
    dest[1] = (i16)v[1];
    dest[2] = (i16)v[2];
}

MINLINE void Vec3i32Copy(const Vec3i32 v, Vec3i32 dest) {
    dest[0] = v[0];
    dest[1] = v[1];
    dest[2] = v[2];
}

MINLINE void Vec3i32ShiftRight(Vec3i32 v, i8 shiftRight) {
    v[0] = v[0] >> shiftRight;
    v[1] = v[1] >> shiftRight;
    v[2] = v[2] >> shiftRight;
}

MINLINE void Vec3i32ShiftLeft(Vec3i32 v, i8 shiftLeft) {
    v[0] = v[0] << shiftLeft;
    v[1] = v[1] << shiftLeft;
    v[2] = v[2] << shiftLeft;
}

MINLINE void Vec3i16ShiftRight(Vec3i16 v, i8 shiftRight) {
    v[0] = (i16)(v[0] >> shiftRight);
    v[1] = (i16)(v[1] >> shiftRight);
    v[2] = (i16)(v[2] >> shiftRight);
}

MINLINE void Vec3i16ShiftLeft(Vec3i16 v, i8 shiftLeft) {
    v[0] = (i16)(v[0] << shiftLeft);
    v[1] = (i16)(v[1] << shiftLeft);
    v[2] = (i16)(v[2] << shiftLeft);
}

MINLINE void Vec3132Midpoint(const Vec3i32 v1, const Vec3i32 v2, Vec3i32 dest) {
    dest[0] = (v1[0] + v2[0]) / 2;
    dest[1] = (v1[1] + v2[1]) / 2;
    dest[2] = (v1[2] + v2[2]) / 2;
}

MINLINE void Vec3i16Neg(Vec3i16 v) {
    v[0] = (i16)(-v[0]);
    v[1] = (i16)(-v[1]);
    v[2] = (i16)(-v[2]);
}

MINLINE void Vec3i16Zero(Vec3i16 v) {
    v[0] = 0;
    v[1] = 0;
    v[2] = 0;
}

MINLINE void Vec3i16Multi16(const Vec3i16 v, i32 s, Vec3i16 dest) {
    dest[0] = (i16)((s * v[0]) >> 15);
    dest[1] = (i16)((s * v[1]) >> 15);
    dest[2] = (i16)((s * v[2]) >> 15);
}

MINLINE void Vec3i16Add(const Vec3i16 v1, const Vec3i16 v2, Vec3i16 dest) {
    dest[0] = (i16)(v1[0] + v2[0]);
    dest[1] = (i16)(v1[1] + v2[1]);
    dest[2] = (i16)(v1[2] + v2[2]);
}

MINLINE void Vec3i32Neg(Vec3i32 v) {
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

MINLINE void Vec3i32Add(const Vec3i32 v1, const Vec3i32 v2, Vec3i32 dest) {
    dest[0] = v1[0] + v2[0];
    dest[1] = v1[1] + v2[1];
    dest[2] = v1[2] + v2[2];
}

MINLINE void Vec3i32Sub(const Vec3i32 v1, const Vec3i32 v2, Vec3i32 dest) {
    dest[0] = v1[0] - v2[0];
    dest[1] = v1[1] - v2[1];
    dest[2] = v1[2] - v2[2];
}

MINLINE void Vec3i32CrossProd(const Vec3i32 v1, const Vec3i32 v2, Vec3i32 dest) {
    dest[0] = (v1[1] * v2[2] - v1[2] * v2[1]) >> 15;
    dest[1] = (v1[2] * v2[0] - v1[0] * v2[2]) >> 15;
    dest[2] = (v1[0] * v2[1] - v1[1] * v2[0]) >> 15;
}

/*
 * Calc cross product & permutate x,y,z -> y,z,x
 *
 * This permutation is equivalent to rotating 120 degrees around {1,1,1}, or
 * two 90 degree rotations around the y then x-axis.
 */
MINLINE void Vec3i32CrossProdNormal(const Vec3i32 v1, const Vec3i16 v2, Vec3i32 dest) {
    dest[0] = (v1[2] * v2[0] - v1[0] * v2[2]) >> 15; // y
    dest[1] = (v1[0] * v2[1] - v1[1] * v2[0]) >> 15; // z
    dest[2] = (v1[1] * v2[2] - v1[2] * v2[1]) >> 15; // x
}

MINLINE i32 Vec3i16DotProd(const Vec3i16 v1, const Vec3i16 v2) {
    return (i32) v1[0] * (i32) v2[0] + (i32) v1[1] * (i32) v2[1] + (i32) v1[2] * (i32) v2[2];
}

MINLINE i32 Vec3i16i32DotProd(const Vec3i16 v1, const Vec3i32 v2) {
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

MINLINE i32 Vec3i32DotProd(const Vec3i32 v1, const Vec3i32 v2) {
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

MINLINE void Vec3i16Normalise(const Vec3i16 in, Vec3i16 out) {
    i32 d = in[0] * (i32)in[0] + in[1] * (i32)in[1] + in[2] * (i32)in[2];
    u32 scale = FMath_SqrtFunc32(d);
    u32 scaleInv = 0x7fffffff / scale;
    out[0] = (i16)((in[0] * (i32)scaleInv) >> 16);
    out[1] = (i16)((in[1] * (i32)scaleInv) >> 16);
    out[2] = (i16)((in[2] * (i32)scaleInv) >> 16);
}

MINLINE i8 GetScaleBelow(const Vec3i32 v, i32 max) {
    i32 x = v[0];
    i32 y = v[1];
    i32 z = v[2];

    if (x < 0) {
        x = -x;
    }
    if (y < 0) {
        y = -y;
    }
    if (z < 0) {
        z = -z;
    }

    u32 o = x | y | z;
    i8 scale = 0;
    while (o > max) {
        o >>= 1;
        scale++;
    }

    return scale;
}

MINLINE i8 GetScaleBelow0x4000(const Vec3i32 v) {
    i32 x = v[0];
    if (x < 0) {
        x = -x;
    }

    i32 y = v[1];
    if (y < 0) {
        y = -y;
    }

    i32 z = v[2];
    if (z < 0) {
        z = -z;
    }

    u32 o = x | y | z;
#ifdef __GNUCC__
    i8 scale = 0;
    while (o >= 0x4000) {
        o >>= 1;
        scale++;
    }
#else
    i8 scale = (i8)__builtin_clz(o);
    if (scale >= 18) {
        scale = 0;
    } else {
        scale = (i8)(18 - scale);
    }
#endif
    return scale;
}

MINLINE i16 Vec3i16ScaleBelow0x4000(Vec3i32 v) {
    i8 scale = GetScaleBelow0x4000(v);
    v[0] >>= scale;
    v[1] >>= scale;
    v[2] >>= scale;
    return scale;
}

// Converts a 32bit integer to a Float16
// 15bits are kept, exponents the rest
MINLINE Float16 Float16MakeFromInt(i32 val) {
    Float16 f16 = {0, 0};
    if (!val) {
        f16.v = 0;
        f16.p = -0x7;
    } else {
        f16.p = F32_FRAC_BITS; // *input* is 32 bits
        // count number of leading 0s or 1s (0 for positive numbers, 1 for neg), minus 1 (for the sign bit).
        while (val > 0 == !(val & 0x40000000)) {
            val <<= 1;
            f16.p--;
        }
        f16.v = (i16) (val >> 16);
    }

    return f16;
}

MINLINE Float32 Float32MakeFromInt(i32 val) {
    Float32 f32 = {0, 0};
    if (!val) {
        f32.v = 0;
        f32.p = -0x7;
    } else {
        f32.p = F32_FRAC_BITS; // input size matches internal value size
        // count number of leading 0s or 1s (0 for positive numbers, 1 for neg), minus 1 (for the sign bit).
        while (val > 0 == !(val & 0x40000000)) {
            val <<= 1;
            f32.p--;
        }
        f32.v = val;
    }

    return f32;
}

MINLINE i16 FloatRebase(i16* bits, i16 paramIn) {
    u16 param = (u16) paramIn;
    if (param) {
        do {
            param <<= 1;
            (*bits)--;
        } while (!(param & 0x8000));

        param >>= 1;
        (*bits)++;
    }

    return (i16) param;
}

// Rebase positive number, so msb is set
MINLINE Float16 Float16uRebase(Float16 val) {
    u16 param = (u16) val.v;
    if (param) {
        do {
            param <<= 1;
            val.p--;
        } while (!(param & 0x8000));

        param >>= 1;
        val.p++;
        val.v = (i16)param;
    }

    return val;
}

MINLINE Float32 Float32Rebase(Float32 f32) {
    // count number of leading 0s or 1s (0 for positive numbers, 1 for neg), minus 1 (for the sign bit).
    // this can be achieved in asm quite easily using the overflow and carry flags set by <<
    if (f32.v > 0) {
#ifdef __GNUC__
        int c = __builtin_clz(f32.v);
        if (c > 1) {
            f32.v = (i32)(f32.v << (i32)(c - 1));
            f32.p = (i16)(f32.p - (i16)(c - 1));
        }
#else
        while (!(f32.v & 0x40000000)) {
            f32.v <<= 1;
            f32.p--;
        }
#endif
    } else if (f32.v < 0) {
        i32 v = (i32)(((i32)~f32.v)+1);
#ifdef __GNUC__
        int c = __builtin_clz(v);
        if (c > 1) {
            v = (i32)(v << (i32)(c - 1));
            f32.p = (i16)(f32.p - (i16)(c - 1));
        }
#else
        while (!(v & 0x40000000)) {
            v <<= 1;
            f32.p--;
        }
#endif
        f32.v = (i32)(~((i32)v-1));
    }

    return f32;
}

MINLINE Float16 Float16Rebase(Float16 f16) {
#ifdef __GNUC__
    if (f16.v > 0) {
        int c = __builtin_clz(f16.v) - 16;
        if (c > 1) {
            f16.v = (i16)(f16.v << (i16)(c - 1));
            f16.p = (i16)(f16.p - (i16)(c - 1));
        }
    } else if (f16.v < 0) {
        i16 v = (i16)(((i16)~f16.v) + 1);
        int c = __builtin_clz(v) - 16;
        if (c > 1) {
            v = (i16)(v << (i16)(c - 1));
            f16.p = (i16)(f16.p - (i16)(c - 1));
        }
        f16.v = (i16)(~((i16)v-1));
    }
#else
    // count number of leading 0s or 1s (0 for positive numbers, 1 for neg), minus 1 (for the sign bit).
    // this can be achieved in asm quite easily using the overflow and carry flags set by <<
    while (f16.v > 0 == !(f16.v & 0x4000)) {
        f16.v <<= 1;
        f16.p--;
    }
#endif
    return f16;
}

MINLINE Float16 Float16Mult(Float16 f1, Float16 f2) {
    Float16 f16;
    f16.p = (i16)(f1.p + f2.p);
    i32 v1 = f1.v;
    i32 v2 = f2.v;
    i16 x = (i16)((v1 * v2) >> 16);
    if (x < 0x4000) {
        f16.v = (i16)(x << 1);
    } else {
        f16.v = x;
        f16.p += 1;
    }
    return f16;
}

MINLINE Float32 Float16Mult32(Float16 f1, Float16 f2) {
    Float32 f32;
    f32.p = (i16)(f1.p + f2.p);
    i16 v1 = f1.v;
    i16 v2 = f2.v;
    i32 x = ((i32)v1 * v2);
    if (x < 0x40000000) {
        f32.v = x << 1;
    } else {
        f32.v = x;
        f32.p += 1;
    }
    return f32;
}

MINLINE Float32 Float32Add(Float32 f1, Float32 f2) {
    Float32 f32;
    i16 dp = (i16)(f1.p - f2.p);
    i16 p;
    if (dp > 0) {
        if (dp >= F32_FRAC_BITS) {
            return f1;
        }
        f2.v >>= dp;
        p = f1.p;
    } else if (dp < 0) {
        dp = (i16)(-dp);
        p = f2.p;
        if (dp >= F32_FRAC_BITS) {
            return f2;
        }
        f1.v >>= dp;
    } else {
        p = f2.p;
    }

#ifdef __GNUC__
    if (__builtin_add_overflow(f1.v, f2.v, &f32.v)) {
        f32.v = (i32)((f1.v >> 1) + (f2.v >> 1));
        f32.p = (i16)(p + 1);
        return f32;
    } else {
        f32.v = (i32)(f1.v + f2.v);
        f32.p = p;
        return Float32Rebase(f32);
    }
#else
    i32 v = f1.v + f2.v;
    if (((f1.v < 0) && (f2.v < 0) && (v >= 0)) ||
        ((f1.v > 0) && (f2.v > 0) && (v < 0))) {
        f32.v = (f1.v >> 1) + (f2.v >> 1);
        f32.p = p + 1;
        return f32;
    } else {
        f32.v = v;
        f32.p = p;
        return Float32Rebase(f32);
    }
#endif
}

MINLINE Float32 Float32Sub(Float32 f1, Float32 f2) {
    Float32 f32;
    i16 dp = (i16)(f1.p - f2.p);
    i16 p;
    if (dp > 0) {
        if (dp >= F32_FRAC_BITS) {
            return f1;
        }
        f2.v >>= dp;
        p = f1.p;
    } else if (dp < 0) {
        dp = (i16)(-dp);
        p = f2.p;
        if (dp >= F32_FRAC_BITS) {
            f1.v = 0;
        } else {
            f1.v >>= dp;
        }
    } else {
        p = f2.p;
    }
#ifdef __GNUC__
    if (__builtin_sub_overflow(f1.v, f2.v, &f32.v)) {
        f32.v = (f1.v >> 1) - (f2.v >> 1);
        f32.p = (i16)(p + 1);
        return f32;
    } else {
        f32.v = f1.v - f2.v;
        f32.p = p;
        return Float32Rebase(f32);
    }
#else
    i32 v = f1.v - f2.v;
    if (((f1.v < 0) && (f2.v > 0) && (v > 0)) ||
        ((f1.v >= 0) && (f2.v < 0) && (v < 0))) {
        f32.v = (f1.v >> 1) - (f2.v >> 1);
        f32.p = (i16)(p + 1);
        return f32;
    } else {
        f32.v = v;
        f32.p = p;
        return Float32Rebase(f32);
    }
#endif
}

MINLINE Float32 Float32Div(Float32 v1, Float32 v2) {
    if (v2.v == 0) {
        return (Float32) {(i16)0x7fffffff, 0x40};
    }
    i16 p = (i16)(v1.p - v2.p);
    i64 v = (i64)v1.v << 30;
    v /= v2.v;
    i32 v32 = (i32) v;
    if (v32 < 0x40000000 && v32 > -0x40000000) {
        v32 <<= 1;
    } else {
        p += 1;
    }
    return (Float32) {v32, p};
}

MINLINE Float16 Float32Convert16(Float32 f32) {
    Float16 f16;
    f16.v = (i16)(f32.v >> 16);
    f16.p = f32.p;
    return f16;
}

MINLINE Float32 Float16Convert32(Float16 f16) {
    Float32 f32;
    f32.v = ((i32)f16.v) << 15;
    f32.p = f16.p;
    return f32;
}

MINLINE Float16 Float16Add(Float16 f1, Float16 f2) {
    Float16 f16;
    i16 dp = (i16)(f1.p - f2.p);
    i16 p;
    if (dp > 0) {
        if (dp >= 0xf) {
            return f1;
        }
        f2.v = (i16)(f2.v >> dp);
        p = f1.p;
    } else if (dp < 0) {
        dp = (i16)(-dp);
        p = f2.p;
        if (dp >= 0xf) {
            return f2;
        }
        f2.v = (i16)(f2.v >> dp);
    } else {
        p = f2.p;
    }
#ifdef __GNUC__
    if (__builtin_add_overflow(f1.v, f2.v, &f16.v)) {
        f16.v = (i16)((f1.v >> 1) + (f2.v >> 1));
        f16.p = (i16)(p + 1);
        return f16;
    } else {
        f16.v = (i16)(f1.v + f2.v);
        f16.p = p;
        return Float16Rebase(f16);
    }
#else
    i16 v = (i16)(f1.v + f2.v);
    if (((f1.v < 0) && (f2.v < 0) && (v >= 0)) ||
        ((f1.v > 0) && (f2.v > 0) && (v < 0))) {
        f16.v = (i16)((f1.v >> 1) + (f2.v >> 1));
        f16.p = (i16)(p + 1);
        return f16;
    } else {
        f16.v = v;
        f16.p = p;
        return Float16Rebase(f16);
    }
#endif
}

MINLINE Float16 Float16Sub(Float16 f1, Float16 f2) {
    Float16 f16;
    i16 dp = (i16)(f1.p - f2.p);
    i16 p;
    if (dp > 0) {
        if (dp >= 0xf) {
            return f1;
        }
        f2.v = (i16)(f2.v >> dp);
        p = f1.p;
    } else if (dp < 0) {
        dp = (i16)(-dp);
        p = f2.p;
        if (dp >= 0xf) {
            f1.v = 0;
        } else {
            f1.v = (i16)(f1.v >> dp);
        }
    } else {
        p = f1.p;
    }
#ifdef __GNUC__
    if (__builtin_sub_overflow(f1.v, f2.v, &f16.v)) {
        f16.v = (i16)((f1.v >> 1) - (f2.v >> 1));
        f16.p = (i16)(p + 1);
        return f16;
    } else {
        f16.v = (i16)(f1.v - f2.v);
        f16.p = p;
        return Float16Rebase(f16);
    }
#else
    i16 v = (i16)(f1.v - f2.v);
    if (((f1.v < 0) && (f2.v > 0) && (v > 0)) ||
        ((f1.v > 0) && (f2.v < 0) && (v < 0))) {
        f16.v = (i16)((f1.v >> 1) - (f2.v >> 1));
        f16.p = (i16)(p + 1);
        return f16;
    } else {
        f16.v = v;
        f16.p = p;
        return Float16Rebase(f16);
    }
#endif
}

MINLINE Float16 Float16Div(Float16 v1, Float16 v2) {
    if (v2.v == 0) {
        return (Float16) {(i16)0x7fff, 0x40};
    }
    i16 p = (i16)(v1.p - v2.p);
    i32 v = v1.v << 14;
    v /= v2.v;
    i16 v16 = (i16) v;
    if (v16 < 0x4000 && v16 > -0x4000) {
        v16 <<= 1;
    } else {
        p += 1;
    }
    return (Float16) {v16, p};
}

MINLINE Float16 Float16Mult16(Float16 v1, Float16 v2) {
    i16 p = (i16)(v2.p + v1.p);
    i32 v = v2.v;
    v *= (i32) v1.v;
    i16 v16 = (i16) (v >> 15);
    return (Float16){.v = v16, .p = p};
}

// Extract whole number
MINLINE Float16 Float16Extract(Float16 v1) {
    v1.p = (i16)(15 - v1.p);
    if (v1.v == 0) {
        return v1;
    } else if (v1.p < 0) {
        if (v1.v < 0) {
            return (Float16) {-0x8000, 0};
        } else {
            return (Float16) {0x7fff, 0};
        }
    }
    i16 v = (i16)(v1.v >> v1.p);
    return (Float16){v, v1.p};
}

MINLINE Float16 Float32Sqrt(Float32 f1) {
    u32 v = f1.v;
    i16 p = f1.p;
    if (p & 0x1) {
        p = (i16)((p >> 1) + 1);
    } else {
        v <<= 1;
        p >>= 1;
    }
    u32 sqrt = FMath_SqrtFunc32(v) >> 1;
    Float16 res = {(i16) sqrt, p};
    return Float16uRebase(res);
}

MINLINE Float16 Float16Sqrt(Float16 f1) {
    u16 v = f1.v;
    i16 p = f1.p;
    if (p & 0x1) {
        p = (i16)((p >> 1) + 1);
    } else {
        v <<= 1;
        p >>= 1;
    }
    u32 sqrt = FMath_SqrtFunc32(v << 16) >> 1;
    return (Float16) {(i16) sqrt, p};
}

MINLINE double Float32ieee(Float32 value) {
    if (value.v == 0) {
        return 0.0;
    }

    u64 b = 0;
    i32 e = (value.p - 1) + 1023;
    if (e > 2047 || e < 0) {
        MLogf("exponent out of range: %d", e);
        e = 0;
    }

    u32 v = 0;
    if (value.v < 0) {
        b = 1;
        b <<= 11;
        v = (-value.v);
        if (v & 0x80000000) {
            e += 1;
        }
    } else {
        v = value.v;
    }

    v &= 0x3fffffff;
    b |= e;
    b <<= 30;
    b |= v;
    b <<= 22;

    return *((double*) ((void*) &b));
}

MINLINE float Float16ieee(Float16 value) {
    if (value.v == 0) {
        return 0.0f;
    }

    u32 b = 0;
    i32 e = (value.p - 1) + 127;
    if (e > 255 || e < 0) {
        MLogf("exponent out of range: %d", e);
        e = 0;
    }

    u32 v = 0;
    if (value.v < 0) {
        b = 1;
        b <<= 8;
        v = (-value.v);
        if (v & 0x8000) {
            e += 1;
        }
    } else {
        v = value.v;
    }

    v &= 0x3fff;
    v <<= 9;
    b |= e;
    b <<= 23;
    b |= v;

    return *((float*) ((void*) &b));
}

int I64FormatDistance(i64 distance, char* buffer, u32 bufferSize);
int Float16FormatDistance(Float16 distance, char* buffer, u32 bufferSize);
int Float32FormatDistance(Float32 distance, char* buffer, u32 bufferSize);

#ifdef __cplusplus
}
#endif

#endif
