#include "common.h"
#include "m2c.h"

double frexp(double value, int *eptr) {
#ifdef HOST_LE
    /* PC port: the exponent is in the high word, which a little-endian host
       keeps second (as in exp()). Reading the first word took low mantissa
       bits instead: frexp returned e = -1022 with the value unchanged, so
       log() and pow() were wrong for every argument but 1.0. Car_Create's
       log(offroad) made the rear tyres' grip factor about -150 for the cars
       with an offroad value other than 1.0, which then spun off at 200 m/s. */
    u32 *hi = (u32 *)&value + 1;

    if (value == 0.0) {
        *eptr = 0;
        return 0.0;
    }
    *eptr = ((*hi >> 20) & 0x7FF) - 0x3FE;
    *hi = (*hi & 0x800FFFFF) | 0x3FE00000;
    return value;
#else
    if (value == 0.0) {
        *eptr = 0;
        return 0.0;
    }
    *eptr = ((*(u32 *)&value >> 20) & 0x7FF) - 0x3FE;
    *(u32 *)&value = (*(u32 *)&value & 0x800FFFFF) | 0x3FE00000;
    return value;
#endif
}
