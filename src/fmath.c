//
// Reverse Engineered from:
//   [Frontier: Elite II © David Braben 1993 & 1994](https://en.wikipedia.org/wiki/Frontier:_Elite_II)
//

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
