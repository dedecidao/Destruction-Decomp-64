#include "common.h"
#include "m2c.h"
#ifdef TARGET_PC
/* [audio] synth_rate (port/src/audio.c) */
int Port_SynthRate(void);
float Port_SynthPitchScale(void);
s32 Port_SynthScaleSamples(s32 n);
s32 Port_SynthScaleLowpass(s32 fc);
#endif

typedef struct ALFilter_s {
    struct ALFilter_s *source;
    s32 pad4;
    s32 (*setParam)(void *, s32, void *);
} ALFilter;

typedef struct {
    ALFilter *source;
    u8 pad4[0x14];
    f32 ratio;
    s32 upitch;
    f32 delta;
    s32 first;
    u8 pad28[0x8];
    s32 motion;
} ALResampler;

s32 alResampleParam(void *filter, s32 paramID, void *param)
{
    ALFilter *f = (ALFilter *)filter;
    ALResampler *r = (ALResampler *)filter;
    union { f32 f; s32 i; } data;

    switch (paramID) {
        case 1:
            f->source = (ALFilter *)param;
            break;
        case 4:
            r->delta = 0.0;
            r->first = 1;
            r->motion = 0;
            r->upitch = 0;
            if (f->source)
                (*f->source->setParam)(f->source, 4, 0);
            break;
        case 9:
            r->motion = 1;
            if (f->source)
                (*f->source->setParam)(f->source, 9, 0);
            break;
        case 7:
            data.i = (s32)param;
#ifdef TARGET_PC
            r->ratio = data.f * Port_SynthPitchScale(); /* the driver's ratios assume 22050 Hz */
#else
            r->ratio = data.f;
#endif
            break;
        case 8:
            r->upitch = 1;
            break;
        default:
            if (f->source)
                (*f->source->setParam)(f->source, paramID, param);
            break;
    }
    return 0;
}
