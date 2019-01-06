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

static inline u32 incMod(u32 Val, u32 Mod, u32 Increment = 1) {
    return ((Val + Increment) % Mod);
}

#endif // TYPES_H
