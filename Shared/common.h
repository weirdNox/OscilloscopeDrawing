#if !defined(TYPES_H)
#define TYPES_H

#define arrayCount(Arr) ((sizeof(Arr))/(sizeof(*Arr)))

typedef  uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef   int8_t  s8;
typedef  int16_t s16;
typedef  int32_t s32;

static inline u8 clamp(u8 Min, u8 Val, u8 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}

static inline s32 clamp(s32 Min, s32 Val, s32 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}

enum {
    MaxFrames = 10,
    MaxActive = 300, // NOTE(nox): Limit to achieve 30 FPS
    FPS = 30,
    MinFrameTimeMs = (1000 + FPS - 1)/FPS,
};


// NOTE(nox): Keeping the grid size a power of 2 with exponent < 8 let's us encode the coordinate + Z
// information in a single byte.
//
// An exponent of 6 (2^6 = 64) seems a good trade-off, because the drawings contain enough detail to be
// perceptible, without wasting many bytes when dragging and there is still space to encode the Z setting.
//
// The 12-bit code to send and our encoding will be as follows (x means ignored and V is the value we want):
//                                 MSB | LSB
// To send             |   |   |   |   |   |   | 0 | 0 | 0 | 0 | 0 | 0 |
// X           | x | Z | V | V | V | V | V | V |
// Y           | x | x | V | V | V | V | V | V |

#define inputMsb(Val) ((Val >> 2) & 0x0F)
#define inputLsb(Val) ((Val << 6) & 0xC0)
enum {
    DacPrecision = 12,
    GridSize = 1<<6,
    ZDisableBit = 1<<6,
};


#endif // TYPES_H
