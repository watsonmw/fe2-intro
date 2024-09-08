//
// Reverse Engineered from:
//   [Frontier: Elite II © David Braben 1993 & 1994](https://en.wikipedia.org/wiki/Frontier:_Elite_II)
//

#include "fmath.h"

#include "math.h"
#include "mlib.h"

#ifdef FMATH_USE_FLOAT_SIN_ACOS
i16 FMath_sine[4096];
i16 FMath_arccos[128];

void FMath_BuildLookupTables() {
    int size = sizeof(FMath_sine) / sizeof(i16) / 2;
    double d = M_PI / (double)size;
    for (int i = 0; i <  size; i++) {
        double v = sin(i * d);
        FMath_sine[i] = (i16)(v * 0x7fff);
    }

#ifdef PRINT_TABLE
    MLog("i16 FMath_sine[] = {");
    for (int j = 0; j < size; j += 16) {
        MLogf("    0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,",
              FMath_sine[j],   FMath_sine[j+1], FMath_sine[j+2], FMath_sine[j+3],
              FMath_sine[j+4], FMath_sine[j+5], FMath_sine[j+6], FMath_sine[j+7],
              FMath_sine[j+8], FMath_sine[j+9], FMath_sine[j+10], FMath_sine[j+11],
              FMath_sine[j+12], FMath_sine[j+13], FMath_sine[j+14], FMath_sine[j+15]);
    }
    MLog("};");
#endif

    int asize = sizeof(FMath_arccos) / sizeof(i16);
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

#ifdef PRINT_TABLE
    MLog("i16 FMath_arccos[] = {");
    for (int j = 0; j < asize; j += 16) {
        MLogf("    0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,",
              FMath_arccos[j],   FMath_arccos[j+1], FMath_arccos[j+2], FMath_arccos[j+3],
              FMath_arccos[j+4], FMath_arccos[j+5], FMath_arccos[j+6], FMath_arccos[j+7],
              FMath_arccos[j+8], FMath_arccos[j+9], FMath_arccos[j+10], FMath_arccos[j+11],
              FMath_arccos[j+12], FMath_arccos[j+13], FMath_arccos[j+14], FMath_arccos[j+15]);
    }
    MLog("};");
#endif
}

#else
i16 FMath_sine[] = {
    0x0000, 0x0032, 0x0064, 0x0096, 0x00c9, 0x00fb, 0x012d, 0x015f, 0x0192, 0x01c4, 0x01f6, 0x0228, 0x025b, 0x028d, 0x02bf, 0x02f1,
    0x0324, 0x0356, 0x0388, 0x03ba, 0x03ed, 0x041f, 0x0451, 0x0483, 0x04b6, 0x04e8, 0x051a, 0x054c, 0x057e, 0x05b1, 0x05e3, 0x0615,
    0x0647, 0x067a, 0x06ac, 0x06de, 0x0710, 0x0742, 0x0774, 0x07a7, 0x07d9, 0x080b, 0x083d, 0x086f, 0x08a1, 0x08d4, 0x0906, 0x0938,
    0x096a, 0x099c, 0x09ce, 0x0a00, 0x0a32, 0x0a65, 0x0a97, 0x0ac9, 0x0afb, 0x0b2d, 0x0b5f, 0x0b91, 0x0bc3, 0x0bf5, 0x0c27, 0x0c59,
    0x0c8b, 0x0cbd, 0x0cef, 0x0d21, 0x0d53, 0x0d85, 0x0db7, 0x0de9, 0x0e1b, 0x0e4d, 0x0e7f, 0x0eb1, 0x0ee3, 0x0f15, 0x0f47, 0x0f79,
    0x0fab, 0x0fdc, 0x100e, 0x1040, 0x1072, 0x10a4, 0x10d6, 0x1107, 0x1139, 0x116b, 0x119d, 0x11cf, 0x1200, 0x1232, 0x1264, 0x1296,
    0x12c7, 0x12f9, 0x132b, 0x135d, 0x138e, 0x13c0, 0x13f2, 0x1423, 0x1455, 0x1486, 0x14b8, 0x14ea, 0x151b, 0x154d, 0x157e, 0x15b0,
    0x15e1, 0x1613, 0x1644, 0x1676, 0x16a7, 0x16d9, 0x170a, 0x173c, 0x176d, 0x179f, 0x17d0, 0x1801, 0x1833, 0x1864, 0x1895, 0x18c7,
    0x18f8, 0x1929, 0x195b, 0x198c, 0x19bd, 0x19ee, 0x1a20, 0x1a51, 0x1a82, 0x1ab3, 0x1ae4, 0x1b15, 0x1b46, 0x1b78, 0x1ba9, 0x1bda,
    0x1c0b, 0x1c3c, 0x1c6d, 0x1c9e, 0x1ccf, 0x1d00, 0x1d31, 0x1d62, 0x1d93, 0x1dc3, 0x1df4, 0x1e25, 0x1e56, 0x1e87, 0x1eb8, 0x1ee8,
    0x1f19, 0x1f4a, 0x1f7b, 0x1fab, 0x1fdc, 0x200d, 0x203d, 0x206e, 0x209f, 0x20cf, 0x2100, 0x2130, 0x2161, 0x2191, 0x21c2, 0x21f2,
    0x2223, 0x2253, 0x2284, 0x22b4, 0x22e4, 0x2315, 0x2345, 0x2375, 0x23a6, 0x23d6, 0x2406, 0x2436, 0x2467, 0x2497, 0x24c7, 0x24f7,
    0x2527, 0x2557, 0x2587, 0x25b7, 0x25e7, 0x2617, 0x2647, 0x2677, 0x26a7, 0x26d7, 0x2707, 0x2737, 0x2767, 0x2797, 0x27c6, 0x27f6,
    0x2826, 0x2856, 0x2885, 0x28b5, 0x28e5, 0x2914, 0x2944, 0x2973, 0x29a3, 0x29d2, 0x2a02, 0x2a31, 0x2a61, 0x2a90, 0x2ac0, 0x2aef,
    0x2b1e, 0x2b4e, 0x2b7d, 0x2bac, 0x2bdb, 0x2c0b, 0x2c3a, 0x2c69, 0x2c98, 0x2cc7, 0x2cf6, 0x2d25, 0x2d54, 0x2d83, 0x2db2, 0x2de1,
    0x2e10, 0x2e3f, 0x2e6e, 0x2e9d, 0x2ecc, 0x2efa, 0x2f29, 0x2f58, 0x2f86, 0x2fb5, 0x2fe4, 0x3012, 0x3041, 0x306f, 0x309e, 0x30cc,
    0x30fb, 0x3129, 0x3158, 0x3186, 0x31b4, 0x31e3, 0x3211, 0x323f, 0x326d, 0x329c, 0x32ca, 0x32f8, 0x3326, 0x3354, 0x3382, 0x33b0,
    0x33de, 0x340c, 0x343a, 0x3468, 0x3496, 0x34c3, 0x34f1, 0x351f, 0x354d, 0x357a, 0x35a8, 0x35d6, 0x3603, 0x3631, 0x365e, 0x368c,
    0x36b9, 0x36e7, 0x3714, 0x3741, 0x376f, 0x379c, 0x37c9, 0x37f6, 0x3824, 0x3851, 0x387e, 0x38ab, 0x38d8, 0x3905, 0x3932, 0x395f,
    0x398c, 0x39b9, 0x39e6, 0x3a12, 0x3a3f, 0x3a6c, 0x3a99, 0x3ac5, 0x3af2, 0x3b1f, 0x3b4b, 0x3b78, 0x3ba4, 0x3bd1, 0x3bfd, 0x3c29,
    0x3c56, 0x3c82, 0x3cae, 0x3cdb, 0x3d07, 0x3d33, 0x3d5f, 0x3d8b, 0x3db7, 0x3de3, 0x3e0f, 0x3e3b, 0x3e67, 0x3e93, 0x3ebf, 0x3eeb,
    0x3f16, 0x3f42, 0x3f6e, 0x3f99, 0x3fc5, 0x3ff0, 0x401c, 0x4047, 0x4073, 0x409e, 0x40ca, 0x40f5, 0x4120, 0x414c, 0x4177, 0x41a2,
    0x41cd, 0x41f8, 0x4223, 0x424e, 0x4279, 0x42a4, 0x42cf, 0x42fa, 0x4325, 0x4350, 0x437a, 0x43a5, 0x43d0, 0x43fa, 0x4425, 0x444f,
    0x447a, 0x44a4, 0x44cf, 0x44f9, 0x4523, 0x454e, 0x4578, 0x45a2, 0x45cc, 0x45f6, 0x4620, 0x464a, 0x4674, 0x469e, 0x46c8, 0x46f2,
    0x471c, 0x4746, 0x476f, 0x4799, 0x47c3, 0x47ec, 0x4816, 0x483f, 0x4869, 0x4892, 0x48bc, 0x48e5, 0x490e, 0x4938, 0x4961, 0x498a,
    0x49b3, 0x49dc, 0x4a05, 0x4a2e, 0x4a57, 0x4a80, 0x4aa9, 0x4ad2, 0x4afa, 0x4b23, 0x4b4c, 0x4b74, 0x4b9d, 0x4bc5, 0x4bee, 0x4c16,
    0x4c3f, 0x4c67, 0x4c8f, 0x4cb8, 0x4ce0, 0x4d08, 0x4d30, 0x4d58, 0x4d80, 0x4da8, 0x4dd0, 0x4df8, 0x4e20, 0x4e48, 0x4e6f, 0x4e97,
    0x4ebf, 0x4ee6, 0x4f0e, 0x4f35, 0x4f5d, 0x4f84, 0x4fac, 0x4fd3, 0x4ffa, 0x5021, 0x5049, 0x5070, 0x5097, 0x50be, 0x50e5, 0x510c,
    0x5133, 0x5159, 0x5180, 0x51a7, 0x51ce, 0x51f4, 0x521b, 0x5241, 0x5268, 0x528e, 0x52b5, 0x52db, 0x5301, 0x5328, 0x534e, 0x5374,
    0x539a, 0x53c0, 0x53e6, 0x540c, 0x5432, 0x5458, 0x547d, 0x54a3, 0x54c9, 0x54ef, 0x5514, 0x553a, 0x555f, 0x5585, 0x55aa, 0x55cf,
    0x55f4, 0x561a, 0x563f, 0x5664, 0x5689, 0x56ae, 0x56d3, 0x56f8, 0x571d, 0x5742, 0x5766, 0x578b, 0x57b0, 0x57d4, 0x57f9, 0x581d,
    0x5842, 0x5866, 0x588a, 0x58af, 0x58d3, 0x58f7, 0x591b, 0x593f, 0x5963, 0x5987, 0x59ab, 0x59cf, 0x59f3, 0x5a16, 0x5a3a, 0x5a5e,
    0x5a81, 0x5aa5, 0x5ac8, 0x5aec, 0x5b0f, 0x5b32, 0x5b56, 0x5b79, 0x5b9c, 0x5bbf, 0x5be2, 0x5c05, 0x5c28, 0x5c4b, 0x5c6d, 0x5c90,
    0x5cb3, 0x5cd6, 0x5cf8, 0x5d1b, 0x5d3d, 0x5d5f, 0x5d82, 0x5da4, 0x5dc6, 0x5de9, 0x5e0b, 0x5e2d, 0x5e4f, 0x5e71, 0x5e93, 0x5eb4,
    0x5ed6, 0x5ef8, 0x5f1a, 0x5f3b, 0x5f5d, 0x5f7e, 0x5fa0, 0x5fc1, 0x5fe2, 0x6004, 0x6025, 0x6046, 0x6067, 0x6088, 0x60a9, 0x60ca,
    0x60eb, 0x610c, 0x612d, 0x614d, 0x616e, 0x618e, 0x61af, 0x61cf, 0x61f0, 0x6210, 0x6230, 0x6251, 0x6271, 0x6291, 0x62b1, 0x62d1,
    0x62f1, 0x6311, 0x6330, 0x6350, 0x6370, 0x638f, 0x63af, 0x63ce, 0x63ee, 0x640d, 0x642d, 0x644c, 0x646b, 0x648a, 0x64a9, 0x64c8,
    0x64e7, 0x6506, 0x6525, 0x6544, 0x6562, 0x6581, 0x65a0, 0x65be, 0x65dd, 0x65fb, 0x6619, 0x6638, 0x6656, 0x6674, 0x6692, 0x66b0,
    0x66ce, 0x66ec, 0x670a, 0x6728, 0x6745, 0x6763, 0x6781, 0x679e, 0x67bc, 0x67d9, 0x67f7, 0x6814, 0x6831, 0x684e, 0x686b, 0x6888,
    0x68a5, 0x68c2, 0x68df, 0x68fc, 0x6919, 0x6935, 0x6952, 0x696e, 0x698b, 0x69a7, 0x69c4, 0x69e0, 0x69fc, 0x6a18, 0x6a34, 0x6a50,
    0x6a6c, 0x6a88, 0x6aa4, 0x6ac0, 0x6adb, 0x6af7, 0x6b13, 0x6b2e, 0x6b4a, 0x6b65, 0x6b80, 0x6b9c, 0x6bb7, 0x6bd2, 0x6bed, 0x6c08,
    0x6c23, 0x6c3e, 0x6c58, 0x6c73, 0x6c8e, 0x6ca8, 0x6cc3, 0x6cdd, 0x6cf8, 0x6d12, 0x6d2c, 0x6d47, 0x6d61, 0x6d7b, 0x6d95, 0x6daf,
    0x6dc9, 0x6de3, 0x6dfc, 0x6e16, 0x6e30, 0x6e49, 0x6e63, 0x6e7c, 0x6e95, 0x6eaf, 0x6ec8, 0x6ee1, 0x6efa, 0x6f13, 0x6f2c, 0x6f45,
    0x6f5e, 0x6f76, 0x6f8f, 0x6fa8, 0x6fc0, 0x6fd9, 0x6ff1, 0x7009, 0x7022, 0x703a, 0x7052, 0x706a, 0x7082, 0x709a, 0x70b2, 0x70ca,
    0x70e1, 0x70f9, 0x7111, 0x7128, 0x7140, 0x7157, 0x716e, 0x7186, 0x719d, 0x71b4, 0x71cb, 0x71e2, 0x71f9, 0x7210, 0x7226, 0x723d,
    0x7254, 0x726a, 0x7281, 0x7297, 0x72ae, 0x72c4, 0x72da, 0x72f0, 0x7306, 0x731c, 0x7332, 0x7348, 0x735e, 0x7374, 0x7389, 0x739f,
    0x73b5, 0x73ca, 0x73df, 0x73f5, 0x740a, 0x741f, 0x7434, 0x7449, 0x745e, 0x7473, 0x7488, 0x749d, 0x74b1, 0x74c6, 0x74db, 0x74ef,
    0x7503, 0x7518, 0x752c, 0x7540, 0x7554, 0x7568, 0x757c, 0x7590, 0x75a4, 0x75b8, 0x75cc, 0x75df, 0x75f3, 0x7606, 0x761a, 0x762d,
    0x7640, 0x7653, 0x7667, 0x767a, 0x768d, 0x76a0, 0x76b2, 0x76c5, 0x76d8, 0x76ea, 0x76fd, 0x7710, 0x7722, 0x7734, 0x7747, 0x7759,
    0x776b, 0x777d, 0x778f, 0x77a1, 0x77b3, 0x77c4, 0x77d6, 0x77e8, 0x77f9, 0x780b, 0x781c, 0x782e, 0x783f, 0x7850, 0x7861, 0x7872,
    0x7883, 0x7894, 0x78a5, 0x78b6, 0x78c6, 0x78d7, 0x78e7, 0x78f8, 0x7908, 0x7919, 0x7929, 0x7939, 0x7949, 0x7959, 0x7969, 0x7979,
    0x7989, 0x7998, 0x79a8, 0x79b8, 0x79c7, 0x79d7, 0x79e6, 0x79f5, 0x7a04, 0x7a14, 0x7a23, 0x7a32, 0x7a41, 0x7a4f, 0x7a5e, 0x7a6d,
    0x7a7c, 0x7a8a, 0x7a99, 0x7aa7, 0x7ab5, 0x7ac4, 0x7ad2, 0x7ae0, 0x7aee, 0x7afc, 0x7b0a, 0x7b18, 0x7b25, 0x7b33, 0x7b41, 0x7b4e,
    0x7b5c, 0x7b69, 0x7b76, 0x7b83, 0x7b91, 0x7b9e, 0x7bab, 0x7bb8, 0x7bc4, 0x7bd1, 0x7bde, 0x7beb, 0x7bf7, 0x7c04, 0x7c10, 0x7c1c,
    0x7c29, 0x7c35, 0x7c41, 0x7c4d, 0x7c59, 0x7c65, 0x7c70, 0x7c7c, 0x7c88, 0x7c93, 0x7c9f, 0x7caa, 0x7cb6, 0x7cc1, 0x7ccc, 0x7cd7,
    0x7ce2, 0x7ced, 0x7cf8, 0x7d03, 0x7d0e, 0x7d18, 0x7d23, 0x7d2e, 0x7d38, 0x7d42, 0x7d4d, 0x7d57, 0x7d61, 0x7d6b, 0x7d75, 0x7d7f,
    0x7d89, 0x7d93, 0x7d9c, 0x7da6, 0x7db0, 0x7db9, 0x7dc2, 0x7dcc, 0x7dd5, 0x7dde, 0x7de7, 0x7df0, 0x7df9, 0x7e02, 0x7e0b, 0x7e13,
    0x7e1c, 0x7e25, 0x7e2d, 0x7e36, 0x7e3e, 0x7e46, 0x7e4e, 0x7e56, 0x7e5e, 0x7e66, 0x7e6e, 0x7e76, 0x7e7e, 0x7e85, 0x7e8d, 0x7e94,
    0x7e9c, 0x7ea3, 0x7eaa, 0x7eb2, 0x7eb9, 0x7ec0, 0x7ec7, 0x7ece, 0x7ed4, 0x7edb, 0x7ee2, 0x7ee8, 0x7eef, 0x7ef5, 0x7efc, 0x7f02,
    0x7f08, 0x7f0e, 0x7f14, 0x7f1a, 0x7f20, 0x7f26, 0x7f2c, 0x7f31, 0x7f37, 0x7f3c, 0x7f42, 0x7f47, 0x7f4c, 0x7f52, 0x7f57, 0x7f5c,
    0x7f61, 0x7f66, 0x7f6a, 0x7f6f, 0x7f74, 0x7f78, 0x7f7d, 0x7f81, 0x7f86, 0x7f8a, 0x7f8e, 0x7f92, 0x7f96, 0x7f9a, 0x7f9e, 0x7fa2,
    0x7fa6, 0x7fa9, 0x7fad, 0x7fb0, 0x7fb4, 0x7fb7, 0x7fbb, 0x7fbe, 0x7fc1, 0x7fc4, 0x7fc7, 0x7fca, 0x7fcd, 0x7fcf, 0x7fd2, 0x7fd5,
    0x7fd7, 0x7fd9, 0x7fdc, 0x7fde, 0x7fe0, 0x7fe2, 0x7fe4, 0x7fe6, 0x7fe8, 0x7fea, 0x7fec, 0x7fee, 0x7fef, 0x7ff1, 0x7ff2, 0x7ff3,
    0x7ff5, 0x7ff6, 0x7ff7, 0x7ff8, 0x7ff9, 0x7ffa, 0x7ffb, 0x7ffb, 0x7ffc, 0x7ffd, 0x7ffd, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffe,
    0x7fff, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffd, 0x7ffd, 0x7ffc, 0x7ffb, 0x7ffb, 0x7ffa, 0x7ff9, 0x7ff8, 0x7ff7, 0x7ff6,
    0x7ff5, 0x7ff3, 0x7ff2, 0x7ff1, 0x7fef, 0x7fee, 0x7fec, 0x7fea, 0x7fe8, 0x7fe6, 0x7fe4, 0x7fe2, 0x7fe0, 0x7fde, 0x7fdc, 0x7fd9,
    0x7fd7, 0x7fd5, 0x7fd2, 0x7fcf, 0x7fcd, 0x7fca, 0x7fc7, 0x7fc4, 0x7fc1, 0x7fbe, 0x7fbb, 0x7fb7, 0x7fb4, 0x7fb0, 0x7fad, 0x7fa9,
    0x7fa6, 0x7fa2, 0x7f9e, 0x7f9a, 0x7f96, 0x7f92, 0x7f8e, 0x7f8a, 0x7f86, 0x7f81, 0x7f7d, 0x7f78, 0x7f74, 0x7f6f, 0x7f6a, 0x7f66,
    0x7f61, 0x7f5c, 0x7f57, 0x7f52, 0x7f4c, 0x7f47, 0x7f42, 0x7f3c, 0x7f37, 0x7f31, 0x7f2c, 0x7f26, 0x7f20, 0x7f1a, 0x7f14, 0x7f0e,
    0x7f08, 0x7f02, 0x7efc, 0x7ef5, 0x7eef, 0x7ee8, 0x7ee2, 0x7edb, 0x7ed4, 0x7ece, 0x7ec7, 0x7ec0, 0x7eb9, 0x7eb2, 0x7eaa, 0x7ea3,
    0x7e9c, 0x7e94, 0x7e8d, 0x7e85, 0x7e7e, 0x7e76, 0x7e6e, 0x7e66, 0x7e5e, 0x7e56, 0x7e4e, 0x7e46, 0x7e3e, 0x7e36, 0x7e2d, 0x7e25,
    0x7e1c, 0x7e13, 0x7e0b, 0x7e02, 0x7df9, 0x7df0, 0x7de7, 0x7dde, 0x7dd5, 0x7dcc, 0x7dc2, 0x7db9, 0x7db0, 0x7da6, 0x7d9c, 0x7d93,
    0x7d89, 0x7d7f, 0x7d75, 0x7d6b, 0x7d61, 0x7d57, 0x7d4d, 0x7d42, 0x7d38, 0x7d2e, 0x7d23, 0x7d18, 0x7d0e, 0x7d03, 0x7cf8, 0x7ced,
    0x7ce2, 0x7cd7, 0x7ccc, 0x7cc1, 0x7cb6, 0x7caa, 0x7c9f, 0x7c93, 0x7c88, 0x7c7c, 0x7c70, 0x7c65, 0x7c59, 0x7c4d, 0x7c41, 0x7c35,
    0x7c29, 0x7c1c, 0x7c10, 0x7c04, 0x7bf7, 0x7beb, 0x7bde, 0x7bd1, 0x7bc4, 0x7bb8, 0x7bab, 0x7b9e, 0x7b91, 0x7b83, 0x7b76, 0x7b69,
    0x7b5c, 0x7b4e, 0x7b41, 0x7b33, 0x7b25, 0x7b18, 0x7b0a, 0x7afc, 0x7aee, 0x7ae0, 0x7ad2, 0x7ac4, 0x7ab5, 0x7aa7, 0x7a99, 0x7a8a,
    0x7a7c, 0x7a6d, 0x7a5e, 0x7a4f, 0x7a41, 0x7a32, 0x7a23, 0x7a14, 0x7a04, 0x79f5, 0x79e6, 0x79d7, 0x79c7, 0x79b8, 0x79a8, 0x7998,
    0x7989, 0x7979, 0x7969, 0x7959, 0x7949, 0x7939, 0x7929, 0x7919, 0x7908, 0x78f8, 0x78e7, 0x78d7, 0x78c6, 0x78b6, 0x78a5, 0x7894,
    0x7883, 0x7872, 0x7861, 0x7850, 0x783f, 0x782e, 0x781c, 0x780b, 0x77f9, 0x77e8, 0x77d6, 0x77c4, 0x77b3, 0x77a1, 0x778f, 0x777d,
    0x776b, 0x7759, 0x7747, 0x7734, 0x7722, 0x7710, 0x76fd, 0x76ea, 0x76d8, 0x76c5, 0x76b2, 0x76a0, 0x768d, 0x767a, 0x7667, 0x7653,
    0x7640, 0x762d, 0x761a, 0x7606, 0x75f3, 0x75df, 0x75cc, 0x75b8, 0x75a4, 0x7590, 0x757c, 0x7568, 0x7554, 0x7540, 0x752c, 0x7518,
    0x7503, 0x74ef, 0x74db, 0x74c6, 0x74b1, 0x749d, 0x7488, 0x7473, 0x745e, 0x7449, 0x7434, 0x741f, 0x740a, 0x73f5, 0x73df, 0x73ca,
    0x73b5, 0x739f, 0x7389, 0x7374, 0x735e, 0x7348, 0x7332, 0x731c, 0x7306, 0x72f0, 0x72da, 0x72c4, 0x72ae, 0x7297, 0x7281, 0x726a,
    0x7254, 0x723d, 0x7226, 0x7210, 0x71f9, 0x71e2, 0x71cb, 0x71b4, 0x719d, 0x7186, 0x716e, 0x7157, 0x7140, 0x7128, 0x7111, 0x70f9,
    0x70e1, 0x70ca, 0x70b2, 0x709a, 0x7082, 0x706a, 0x7052, 0x703a, 0x7022, 0x7009, 0x6ff1, 0x6fd9, 0x6fc0, 0x6fa8, 0x6f8f, 0x6f76,
    0x6f5e, 0x6f45, 0x6f2c, 0x6f13, 0x6efa, 0x6ee1, 0x6ec8, 0x6eaf, 0x6e95, 0x6e7c, 0x6e63, 0x6e49, 0x6e30, 0x6e16, 0x6dfc, 0x6de3,
    0x6dc9, 0x6daf, 0x6d95, 0x6d7b, 0x6d61, 0x6d47, 0x6d2c, 0x6d12, 0x6cf8, 0x6cdd, 0x6cc3, 0x6ca8, 0x6c8e, 0x6c73, 0x6c58, 0x6c3e,
    0x6c23, 0x6c08, 0x6bed, 0x6bd2, 0x6bb7, 0x6b9c, 0x6b80, 0x6b65, 0x6b4a, 0x6b2e, 0x6b13, 0x6af7, 0x6adb, 0x6ac0, 0x6aa4, 0x6a88,
    0x6a6c, 0x6a50, 0x6a34, 0x6a18, 0x69fc, 0x69e0, 0x69c4, 0x69a7, 0x698b, 0x696e, 0x6952, 0x6935, 0x6919, 0x68fc, 0x68df, 0x68c2,
    0x68a5, 0x6888, 0x686b, 0x684e, 0x6831, 0x6814, 0x67f7, 0x67d9, 0x67bc, 0x679e, 0x6781, 0x6763, 0x6745, 0x6728, 0x670a, 0x66ec,
    0x66ce, 0x66b0, 0x6692, 0x6674, 0x6656, 0x6638, 0x6619, 0x65fb, 0x65dd, 0x65be, 0x65a0, 0x6581, 0x6562, 0x6544, 0x6525, 0x6506,
    0x64e7, 0x64c8, 0x64a9, 0x648a, 0x646b, 0x644c, 0x642d, 0x640d, 0x63ee, 0x63ce, 0x63af, 0x638f, 0x6370, 0x6350, 0x6330, 0x6311,
    0x62f1, 0x62d1, 0x62b1, 0x6291, 0x6271, 0x6251, 0x6230, 0x6210, 0x61f0, 0x61cf, 0x61af, 0x618e, 0x616e, 0x614d, 0x612d, 0x610c,
    0x60eb, 0x60ca, 0x60a9, 0x6088, 0x6067, 0x6046, 0x6025, 0x6004, 0x5fe2, 0x5fc1, 0x5fa0, 0x5f7e, 0x5f5d, 0x5f3b, 0x5f1a, 0x5ef8,
    0x5ed6, 0x5eb4, 0x5e93, 0x5e71, 0x5e4f, 0x5e2d, 0x5e0b, 0x5de9, 0x5dc6, 0x5da4, 0x5d82, 0x5d5f, 0x5d3d, 0x5d1b, 0x5cf8, 0x5cd6,
    0x5cb3, 0x5c90, 0x5c6d, 0x5c4b, 0x5c28, 0x5c05, 0x5be2, 0x5bbf, 0x5b9c, 0x5b79, 0x5b56, 0x5b32, 0x5b0f, 0x5aec, 0x5ac8, 0x5aa5,
    0x5a81, 0x5a5e, 0x5a3a, 0x5a16, 0x59f3, 0x59cf, 0x59ab, 0x5987, 0x5963, 0x593f, 0x591b, 0x58f7, 0x58d3, 0x58af, 0x588a, 0x5866,
    0x5842, 0x581d, 0x57f9, 0x57d4, 0x57b0, 0x578b, 0x5766, 0x5742, 0x571d, 0x56f8, 0x56d3, 0x56ae, 0x5689, 0x5664, 0x563f, 0x561a,
    0x55f4, 0x55cf, 0x55aa, 0x5585, 0x555f, 0x553a, 0x5514, 0x54ef, 0x54c9, 0x54a3, 0x547d, 0x5458, 0x5432, 0x540c, 0x53e6, 0x53c0,
    0x539a, 0x5374, 0x534e, 0x5328, 0x5301, 0x52db, 0x52b5, 0x528e, 0x5268, 0x5241, 0x521b, 0x51f4, 0x51ce, 0x51a7, 0x5180, 0x5159,
    0x5133, 0x510c, 0x50e5, 0x50be, 0x5097, 0x5070, 0x5049, 0x5021, 0x4ffa, 0x4fd3, 0x4fac, 0x4f84, 0x4f5d, 0x4f35, 0x4f0e, 0x4ee6,
    0x4ebf, 0x4e97, 0x4e6f, 0x4e48, 0x4e20, 0x4df8, 0x4dd0, 0x4da8, 0x4d80, 0x4d58, 0x4d30, 0x4d08, 0x4ce0, 0x4cb8, 0x4c8f, 0x4c67,
    0x4c3f, 0x4c16, 0x4bee, 0x4bc5, 0x4b9d, 0x4b74, 0x4b4c, 0x4b23, 0x4afa, 0x4ad2, 0x4aa9, 0x4a80, 0x4a57, 0x4a2e, 0x4a05, 0x49dc,
    0x49b3, 0x498a, 0x4961, 0x4938, 0x490e, 0x48e5, 0x48bc, 0x4892, 0x4869, 0x483f, 0x4816, 0x47ec, 0x47c3, 0x4799, 0x476f, 0x4746,
    0x471c, 0x46f2, 0x46c8, 0x469e, 0x4674, 0x464a, 0x4620, 0x45f6, 0x45cc, 0x45a2, 0x4578, 0x454e, 0x4523, 0x44f9, 0x44cf, 0x44a4,
    0x447a, 0x444f, 0x4425, 0x43fa, 0x43d0, 0x43a5, 0x437a, 0x4350, 0x4325, 0x42fa, 0x42cf, 0x42a4, 0x4279, 0x424e, 0x4223, 0x41f8,
    0x41cd, 0x41a2, 0x4177, 0x414c, 0x4120, 0x40f5, 0x40ca, 0x409e, 0x4073, 0x4047, 0x401c, 0x3ff0, 0x3fc5, 0x3f99, 0x3f6e, 0x3f42,
    0x3f16, 0x3eeb, 0x3ebf, 0x3e93, 0x3e67, 0x3e3b, 0x3e0f, 0x3de3, 0x3db7, 0x3d8b, 0x3d5f, 0x3d33, 0x3d07, 0x3cdb, 0x3cae, 0x3c82,
    0x3c56, 0x3c29, 0x3bfd, 0x3bd1, 0x3ba4, 0x3b78, 0x3b4b, 0x3b1f, 0x3af2, 0x3ac5, 0x3a99, 0x3a6c, 0x3a3f, 0x3a12, 0x39e6, 0x39b9,
    0x398c, 0x395f, 0x3932, 0x3905, 0x38d8, 0x38ab, 0x387e, 0x3851, 0x3824, 0x37f6, 0x37c9, 0x379c, 0x376f, 0x3741, 0x3714, 0x36e7,
    0x36b9, 0x368c, 0x365e, 0x3631, 0x3603, 0x35d6, 0x35a8, 0x357a, 0x354d, 0x351f, 0x34f1, 0x34c3, 0x3496, 0x3468, 0x343a, 0x340c,
    0x33de, 0x33b0, 0x3382, 0x3354, 0x3326, 0x32f8, 0x32ca, 0x329c, 0x326d, 0x323f, 0x3211, 0x31e3, 0x31b4, 0x3186, 0x3158, 0x3129,
    0x30fb, 0x30cc, 0x309e, 0x306f, 0x3041, 0x3012, 0x2fe4, 0x2fb5, 0x2f86, 0x2f58, 0x2f29, 0x2efa, 0x2ecc, 0x2e9d, 0x2e6e, 0x2e3f,
    0x2e10, 0x2de1, 0x2db2, 0x2d83, 0x2d54, 0x2d25, 0x2cf6, 0x2cc7, 0x2c98, 0x2c69, 0x2c3a, 0x2c0b, 0x2bdb, 0x2bac, 0x2b7d, 0x2b4e,
    0x2b1e, 0x2aef, 0x2ac0, 0x2a90, 0x2a61, 0x2a31, 0x2a02, 0x29d2, 0x29a3, 0x2973, 0x2944, 0x2914, 0x28e5, 0x28b5, 0x2885, 0x2856,
    0x2826, 0x27f6, 0x27c6, 0x2797, 0x2767, 0x2737, 0x2707, 0x26d7, 0x26a7, 0x2677, 0x2647, 0x2617, 0x25e7, 0x25b7, 0x2587, 0x2557,
    0x2527, 0x24f7, 0x24c7, 0x2497, 0x2467, 0x2436, 0x2406, 0x23d6, 0x23a6, 0x2375, 0x2345, 0x2315, 0x22e4, 0x22b4, 0x2284, 0x2253,
    0x2223, 0x21f2, 0x21c2, 0x2191, 0x2161, 0x2130, 0x2100, 0x20cf, 0x209f, 0x206e, 0x203d, 0x200d, 0x1fdc, 0x1fab, 0x1f7b, 0x1f4a,
    0x1f19, 0x1ee8, 0x1eb8, 0x1e87, 0x1e56, 0x1e25, 0x1df4, 0x1dc3, 0x1d93, 0x1d62, 0x1d31, 0x1d00, 0x1ccf, 0x1c9e, 0x1c6d, 0x1c3c,
    0x1c0b, 0x1bda, 0x1ba9, 0x1b78, 0x1b46, 0x1b15, 0x1ae4, 0x1ab3, 0x1a82, 0x1a51, 0x1a20, 0x19ee, 0x19bd, 0x198c, 0x195b, 0x1929,
    0x18f8, 0x18c7, 0x1895, 0x1864, 0x1833, 0x1801, 0x17d0, 0x179f, 0x176d, 0x173c, 0x170a, 0x16d9, 0x16a7, 0x1676, 0x1644, 0x1613,
    0x15e1, 0x15b0, 0x157e, 0x154d, 0x151b, 0x14ea, 0x14b8, 0x1486, 0x1455, 0x1423, 0x13f2, 0x13c0, 0x138e, 0x135d, 0x132b, 0x12f9,
    0x12c7, 0x1296, 0x1264, 0x1232, 0x1200, 0x11cf, 0x119d, 0x116b, 0x1139, 0x1107, 0x10d6, 0x10a4, 0x1072, 0x1040, 0x100e, 0x0fdc,
    0x0fab, 0x0f79, 0x0f47, 0x0f15, 0x0ee3, 0x0eb1, 0x0e7f, 0x0e4d, 0x0e1b, 0x0de9, 0x0db7, 0x0d85, 0x0d53, 0x0d21, 0x0cef, 0x0cbd,
    0x0c8b, 0x0c59, 0x0c27, 0x0bf5, 0x0bc3, 0x0b91, 0x0b5f, 0x0b2d, 0x0afb, 0x0ac9, 0x0a97, 0x0a65, 0x0a32, 0x0a00, 0x09ce, 0x099c,
    0x096a, 0x0938, 0x0906, 0x08d4, 0x08a1, 0x086f, 0x083d, 0x080b, 0x07d9, 0x07a7, 0x0774, 0x0742, 0x0710, 0x06de, 0x06ac, 0x067a,
    0x0647, 0x0615, 0x05e3, 0x05b1, 0x057e, 0x054c, 0x051a, 0x04e8, 0x04b6, 0x0483, 0x0451, 0x041f, 0x03ed, 0x03ba, 0x0388, 0x0356,
    0x0324, 0x02f1, 0x02bf, 0x028d, 0x025b, 0x0228, 0x01f6, 0x01c4, 0x0192, 0x015f, 0x012d, 0x00fb, 0x00c9, 0x0096, 0x0064, 0x0032
};

i16 FMath_arccos[] = {
    0x7fff, 0x7eb9, 0x7d73, 0x7c2c, 0x7ae6, 0x799f, 0x7858, 0x7710, 0x75c8, 0x747f, 0x7336, 0x71eb, 0x70a0, 0x6f54, 0x6e06, 0x6cb8,
    0x6b68, 0x6a16, 0x68c3, 0x676f, 0x6619, 0x64c1, 0x6367, 0x620a, 0x60ac, 0x5f4b, 0x5de8, 0x5c82, 0x5b19, 0x59ad, 0x583d, 0x56cb,
    0x5554, 0x53da, 0x525b, 0x50d8, 0x4f51, 0x4dc4, 0x4c31, 0x4a99, 0x48fb, 0x4756, 0x45aa, 0x43f6, 0x423a, 0x4074, 0x3ea5, 0x3cca,
    0x3ae4, 0x38f0, 0x36ee, 0x34db, 0x32b6, 0x307c, 0x2e2a, 0x2bbc, 0x292d, 0x2677, 0x2390, 0x206c, 0x1cf6, 0x190c, 0x146c, 0xe6c,
    0xfffe, 0xf191, 0xeb91, 0xe6f1, 0xe307, 0xdf91, 0xdc6d, 0xd986, 0xd6d0, 0xd441, 0xd1d3, 0xcf81, 0xcd47, 0xcb22, 0xc90f, 0xc70d,
    0xc519, 0xc333, 0xc158, 0xbf89, 0xbdc3, 0xbc07, 0xba53, 0xb8a7, 0xb702, 0xb564, 0xb3cc, 0xb239, 0xb0ac, 0xaf25, 0xada2, 0xac23,
    0xaaa9, 0xa932, 0xa7c0, 0xa650, 0xa4e4, 0xa37b, 0xa215, 0xa0b2, 0x9f51, 0x9df3, 0x9c96, 0x9b3c, 0x99e4, 0x988e, 0x973a, 0x95e7,
    0x9495, 0x9345, 0x91f7, 0x90a9, 0x8f5d, 0x8e12, 0x8cc7, 0x8b7e, 0x8a35, 0x88ed, 0x87a5, 0x865e, 0x8517, 0x83d1, 0x828a, 0x8144,
};

#endif
