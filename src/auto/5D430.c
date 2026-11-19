#include "common.h"
#include "m2c.h"
#ifdef TARGET_PC
/* [audio] synth_rate (port/src/audio.c) */
int Port_SynthRate(void);
float Port_SynthPitchScale(void);
s32 Port_SynthScaleSamples(s32 n);
s32 Port_SynthScaleLowpass(s32 fc);
s32 Port_SynthHeapSize(void);
#endif

typedef struct {
    s32 x0;
    s32 x4;
    void *x8;
    s32 xC;
    void *x10;
    s32 x14;
    void *x18;
    void *x1C;
    void *x20;
    void *x24;
    s32 x28;
    s32 x2C;
    s32 x30;
    s32 x34;
    s32 x38;
    s32 x3C;
} AudioCfg;
extern u8 D_8FC5B0[], D_9EF540[], D_BD4BA0[], D_8FE290[], D_9F5680[], D_BDAC70[];
extern u8 D_80108D60[];
extern u8 D_80130250[], D_800D3200[], D_80133D08[], D_801340E8[];
void Rom_Copy(void *, void *, s32);
void Sound_RelocateBank(void *, void *);
void Audio_InitSynth(AudioCfg *);

void Audio_LoadBanks(void) {
    AudioCfg cfg;

    Rom_Copy(D_8FC5B0, D_80108D60, 0x1D48);
    Rom_Copy(D_9EF540, D_80108D60 + 0x1D48, 0x61A8);
    Rom_Copy(D_BD4BA0, D_80108D60 + 0x7EF0, 0x61A8);
    Sound_RelocateBank(D_80108D60, D_8FE290);
    Sound_RelocateBank(D_80108D60 + 0x1D48, D_9F5680);
    Sound_RelocateBank(D_80108D60 + 0x7EF0, D_BDAC70);
    cfg.x0 = 0;
    cfg.x4 = 0x18;
    cfg.x8 = D_80130250;
    cfg.xC = 0xC;
    cfg.x10 = D_800D3200;
    cfg.x14 = 0x35B60;
    cfg.x18 = D_80108D60;
    cfg.x1C = D_8FE290;
    cfg.x20 = D_80133D08;
    cfg.x24 = D_801340E8;
#ifdef TARGET_PC
    cfg.x2C = Port_SynthRate(); /* [audio] synth_rate: 22050 as the N64, or 44100 */
    if (Port_SynthHeapSize() > 0) {
        /* at 44100 the synthesizer's buffers outgrow the N64's audio heap: its own, at
           the top of the port's RAM (the main heap stops below it, 5C480.c) */
        extern u8 gRamEnd[];
        cfg.x10 = gRamEnd - Port_SynthHeapSize();
        cfg.x14 = Port_SynthHeapSize();
    }
#else
    cfg.x2C = 0x5622;
#endif
    cfg.x28 = 0x100;
    cfg.x30 = 0x1800;
    cfg.x34 = 1;
    cfg.x38 = 0x24;
    cfg.x3C = 0x800;
    Audio_InitSynth(&cfg);
}
