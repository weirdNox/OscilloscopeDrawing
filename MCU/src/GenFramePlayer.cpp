#if GenFramePlayer

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
    LDAC = 1<<9, // RD9
    ZPin = 1<<2, // RD2
    MaxPointsPerFrame = 300,
    DisableZBit = 1<<12,
};

typedef struct {
    u16 X, Y;
} point;

typedef struct {
    u32 RepeatCount;
    u32 PointCount;
    point Points[MaxPointsPerFrame];
} frame;

#include "Frames.h"

Timer2 ZTimer = {};
Timer4 FrameTimer = {};

bool ShouldUpdate = false;
u32 SelectedFrame = 0;
u32 FrameRepeatCount = 0;

static void __attribute__((interrupt)) enableZ() {
    LATDSET = ZPin;
    ZTimer.stop();
    clearIntFlag(_TIMER_2_IRQ);
}

static void __attribute__((interrupt)) setUpdateFlag() {
    ShouldUpdate = true;
    clearIntFlag(_TIMER_4_IRQ);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    TRISDCLR = LDAC | ZPin;
    LATDSET  = LDAC | ZPin;

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

    // NOTE(nox): 30 FPS
    FrameTimer.setFrequency(30);
    FrameTimer.attachInterrupt(setUpdateFlag);
    FrameTimer.start();

    // NOTE(nox): This prevents the transitions from being visible in the XYZ mode, and needs to wait at
    // least the settling time, 6.5us. Giving it some margin, we chose 10us of period.
    ZTimer.setFrequency(100000);
    ZTimer.attachInterrupt(enableZ);

    Serial.println("Setup done.");
}

// NOTE(nox): X and Y are in the range [0, 4096[
static void setCoordinates(u16 X, u16 Y) {
    LATDCLR = (X & DisableZBit) ? ZPin : 0;
    LATDSET = LDAC;

    // NOTE(nox): Multi-Write command - 5.6.2
    u8 Data[] = {(0x40 | (0 << 1) | 1), (0x90 | inputMsb(X)), inputLsb(X),  // Output A
                 (0x40 | (1 << 1) | 1), (0x90 | inputMsb(Y)), inputLsb(Y)}; // Output B
    Wire.beginTransmission(DacAddr);
    Wire.write(Data, arrayCount(Data));
    Wire.endTransmission();

    LATDCLR = LDAC;
    ZTimer.start();
}

void loop() {
    if(ShouldUpdate) {
        frame *Frame = Frames + SelectedFrame;
        for(u32 I = 0; I < Frame->PointCount; ++I) {
            point *P = Frame->Points + I;
            setCoordinates(P->X, P->Y);
        }
        ++FrameRepeatCount;
        if(FrameRepeatCount >= Frame->RepeatCount) {
            FrameRepeatCount = 0;
            SelectedFrame = (SelectedFrame + 1) % arrayCount(Frames);
        }
        ShouldUpdate = false;
    }
}

#endif
