#if !defined(TYPES_H)
#define TYPES_H

#define arrayCount(Arr) ((sizeof(Arr))/(sizeof(*Arr)))

typedef  uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef   int8_t  s8;
typedef  int16_t s16;
typedef  int32_t s32;

static s32 clamp(s32 Min, s32 Val, s32 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}

enum {
    GridSize = 64,
    MaxFrames = 10,
    MaxActive = 300, // NOTE(nox): Limit to achieve 30 FPS
    FPS = 30,
    MinFrameTimeMs = (1000 + FPS - 1)/FPS,
    ZDisableBit = 1<<12,
};

#endif // TYPES_H
