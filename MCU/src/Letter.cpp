#if DrawLetter

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

typedef struct {
    u16 X, Y;
} point;

point FramePoints[] = {
    {2752, 3008}, {2624, 3072}, {2304, 3200}, {2176, 3264}, {1984, 3264}, {1856, 3264},
    {1600, 3200}, {1472, 3008}, {1216, 2816}, {1216, 2624}, {1280, 2432}, {1344, 2240}, {1536, 2112},
    {1728, 1920}, {1920, 1920}, {2112, 1920}, {2304, 1856}, {2432, 1984}, {2688, 1984}, {2816, 2176},
    {2816, 2368}, {2688, 2496}, {2432, 2432}, {2304, 2496}, {2176, 2496}, {2112, 2496}, {1984, 2496},
    {1600, 1088}, {1600, 1216}, {1664, 1216}, {1728, 1152}, {1728, 1088}, {1664, 1088}, {2432, 1216},
    {2496, 1216}, {2560, 1216}, {2560, 1152}, {2432, 1088}, {2496, 1088}, {2496, 1152}, {2560, 704},
    {2240, 512}, {2432, 576}, {1984, 512}, {1792, 512}, {1728, 576}, {1600, 704}, {1600, 768},
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
    for(u32 I = 0; I < arrayCount(FramePoints); ++I) {
        point *P = FramePoints + I;
        setCoordinates(P->X, P->Y);
    }

    delay(30);
}

#endif
