#if Plot

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

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    TRISDCLR = LDAC;
    LATDSET  = LDAC;

    Wire.begin();

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
    u8 DataA[] = {(0x40 | (0 << 1) | 1), (0x90 | inputMsb(X)), inputLsb(X)};
    u8 DataB[] = {(0x40 | (1 << 1) | 1), (0x90 | inputMsb(Y)), inputLsb(Y)};
    Wire.beginTransmission(DacAddr);
    Wire.write(DataA, arrayCount(DataA));
    Wire.write(DataB, arrayCount(DataB));
    Wire.endTransmission();

    LATDCLR = LDAC;
}

void loop() {
    setCoordinates(2048, 1024);
    delay(5000);
}

#endif
