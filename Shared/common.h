#if !defined(TYPES_H)
#define TYPES_H

#define arrayCount(Arr) ((sizeof(Arr))/(sizeof(*Arr)))

typedef  uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef   int8_t  s8;
typedef  int16_t s16;
typedef  int32_t s32;

typedef    float r32;

#ifndef _ARDUINO_H
static inline s32 min(s32 A, s32 B) {
    return A < B ? A : B;
}

static inline u64 min(u64 A, u64 B) {
    return A < B ? A : B;
}

static inline s32 max(s32 A, s32 B) {
    return A > B ? A : B;
}
#endif

static inline s32 clamp(s32 Min, s32 Val, s32 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}

static inline s8 clamp(s8 Min, s8 Val, s8 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}

static inline u8 clamp(u8 Min, u8 Val, u8 Max) {
    if(Val < Min) {
        Val = Min;
    } else if(Val > Max) {
        Val = Max;
    }

    return Val;
}


#endif // TYPES_H
