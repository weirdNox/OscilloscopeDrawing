#if Circles

#include <Arduino.h>
#include "Wire.h"
#include "timer.h"

typedef  uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef   int8_t  s8;
typedef  int16_t s16;
typedef  int32_t s32;

#define arrayCount(Arr) ((sizeof(Arr))/(sizeof(*Arr)))

#define inputMsb(Val) ((Val >> 8) & 0x0F)
#define inputLsb(Val) ((Val >> 0) & 0xFF)

typedef enum {
    DacAddr = 0x60,
} address;

enum {
    LDAC = 1<<9, // RD9
};

u32 Count = 0;

u16 BigSin[] = {
    2048, 2177, 2305, 2432, 2557, 2681, 2802, 2920, 3034, 3145,
    3251, 3353, 3449, 3540, 3625, 3704, 3776, 3842, 3900, 3951,
    3995, 4031, 4059, 4079, 4091, 4095, 4091, 4079, 4059, 4031,
    3995, 3951, 3900, 3842, 3776, 3704, 3625, 3540, 3449, 3353,
    3251, 3145, 3034, 2920, 2802, 2681, 2557, 2432, 2305, 2177,
    2048, 1919, 1791, 1664, 1539, 1415, 1294, 1176, 1062, 951,
    845,  743,  647,  556,  471,  392,  320,  254,  196,  145,
    101,  65,   37,   17,   5,    1,    5,    17,   37,   65,
    101,  145,  196,  254,  320,  392,  471,  556,  647,  743,
    845,  951,  1062, 1176, 1294, 1415, 1539, 1664, 1791, 1919,
};

u16 SmallSin[arrayCount(BigSin)] = {
    2048, 2112, 2176, 2240, 2303, 2364, 2425, 2484, 2541, 2597,
    2650, 2701, 2749, 2794, 2837, 2876, 2913, 2945, 2975, 3000,
    3022, 3040, 3054, 3064, 3070, 3072, 3070, 3064, 3054, 3040,
    3022, 3000, 2975, 2945, 2913, 2876, 2837, 2794, 2749, 2701,
    2650, 2597, 2541, 2484, 2425, 2364, 2303, 2240, 2176, 2112,
    2048, 1984, 1920, 1856, 1793, 1732, 1671, 1612, 1555, 1499,
    1446, 1395, 1347, 1302, 1259, 1220, 1183, 1151, 1121, 1096,
    1074, 1056, 1042, 1032, 1026, 1024, 1026, 1032, 1042, 1056,
    1074, 1096, 1121, 1151, 1183, 1220, 1259, 1302, 1347, 1395,
    1446, 1499, 1555, 1612, 1671, 1732, 1793, 1856, 1920, 1984,
};

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    TRISDCLR = LDAC;
    LATDSET  = LDAC;

    Wire.begin();
    u32 Clock = Wire.setClock(1000000);
    Serial.printf("Clock set to %d\r\n", Clock);

    // NOTE(nox): Sequential write command (A -> D) - 5.6.3
    {
        Wire.beginTransmission(DacAddr);
        Wire.write(0x50);

        u8 ActiveData[] = {0x9F, 0xFF};
        Wire.write(ActiveData, 2);
        Wire.write(ActiveData, 2);

        u8 DisabledData[] = {0x40, 0x00};
        Wire.write(DisabledData, 2);
        Wire.write(DisabledData, 2);

        Wire.endTransmission();
    }
    delay(50);

    Serial.println("Setup done.");
}

// NOTE(nox): X and Y are in the range [0, 4096[
static void setCoordinates(u16 X, u16 Y) {
    LATDSET = LDAC;

    // NOTE(nox): Multi-Write command - 5.6.2
    u8 Data[] = {(0x40 | (0 << 1) | 1), (0x90 | inputMsb(X)), inputLsb(X),  // Output A
                 (0x40 | (1 << 1) | 1), (0x90 | inputMsb(Y)), inputLsb(Y)}; // Output B
    Wire.beginTransmission(DacAddr);
    Wire.write(Data, arrayCount(Data));
    Wire.endTransmission();

    LATDCLR = LDAC;
}

void loop() {
    u16 *Sin;
    if(Count < 15) {
        Sin = BigSin;
    } else {
        Sin = SmallSin;
    }

    for(u32 I = 0; I < arrayCount(BigSin); ++I) {
        setCoordinates(Sin[(I+25) % arrayCount(BigSin)], Sin[I]);
    }

    Count = (Count + 1) % 30;
    delay(23); // NOTE(nox): Frame time = 23ms + 10ms (from 100 setCoordinates!)
}

#endif
