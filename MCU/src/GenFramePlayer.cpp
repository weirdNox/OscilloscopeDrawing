#if GenFramePlayer

#include <Arduino.h>
#include "Wire.h"
#include "timer.h"

#define assert(...)
#include <common.h>
#include <protocol.h>

#define inputMsb(Val) ((Val >> 8) & 0x0F)
#define inputLsb(Val) ((Val >> 0) & 0xFF)

enum {
    DacAddr = 0x60,
    LDAC = 1<<9, // RD9
    ZPin = 1<<2, // RD2
    MaxPointsPerFrame = 300,
    DisableZBit = 1<<12,
    FPB = 80000000,
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

u8 RxBuffer[300] = {0};
u32 RxAvailable = 0;
u32 RxWriteIdx = 0;
u32 RxReadIdx = 0;
bool RxNewFrame = false;

u8 FrameBuffer[7000] = {0};
u32 FrameWriteIdx = 0;
u32 FrameReadIdx = 0;

static inline u32 incMod(u32 Val, u32 Mod) {
    return ((Val + 1) % Mod);
}

static inline void incRxRead() {
    RxReadIdx = incMod(RxReadIdx, arrayCount(RxBuffer));
    --RxAvailable;
}

static void __USER_ISR enableZ() {
    LATDSET = ZPin;
    ZTimer.stop();
    clearIntFlag(_TIMER_2_IRQ);
}

static void __USER_ISR setUpdateFlag() {
    ShouldUpdate = true;
    clearIntFlag(_TIMER_4_IRQ);
}

static void __USER_ISR uartRx() {
    u8 Byte = U1RXREG;

    if(Byte == 0) {
        RxNewFrame = true;
    }

    RxBuffer[RxWriteIdx] = Byte;
    RxWriteIdx = incMod(RxWriteIdx, arrayCount(RxBuffer));
    if(RxWriteIdx == RxReadIdx) {
        RxReadIdx = incMod(RxReadIdx, arrayCount(RxBuffer));
    } else {
        ++RxAvailable;
    }
    clearIntFlag(_UART1_RX_IRQ);
}

void setup() {
    // NOTE(nox): Setup serial communication
    U1BRG = FPB/(4.0*BaudRate)-1;
    U1MODEbits.ON   = 1;
    U1MODEbits.BRGH = 1;
    U1STAbits.UTXEN = 1;
    U1STAbits.URXEN = 1;
    U1STAbits.URXISEL = 0;
    setIntVector(_UART1_VECTOR, uartRx);
    setIntPriority(_UART1_VECTOR, 2, 2);
    clearIntFlag(_UART1_RX_IRQ);
    setIntEnable(_UART1_RX_IRQ);

    TRISDCLR = LDAC | ZPin;
    LATDSET  = LDAC | ZPin;

    Wire.begin();
    u32 Clock = Wire.setClock(1000000);

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

static void unstuffBytes() {
    bool DidUnstuff = false;
    u8 Code = 0xFF, Copy = 0;

    if(RxAvailable) {
        for(; RxBuffer[RxReadIdx]; --Copy) {
            if(Copy) {
                FrameBuffer[FrameWriteIdx] = RxBuffer[RxReadIdx];
                FrameWriteIdx = incMod(FrameWriteIdx, arrayCount(FrameBuffer));
                incRxRead();
            }
            else {
                if(Code != 0xFF) {
                    FrameBuffer[FrameWriteIdx] = 0;
                    FrameWriteIdx = incMod(FrameWriteIdx, arrayCount(FrameBuffer));
                }

                Copy = Code = RxBuffer[RxReadIdx];
                if(Copy-1 > RxAvailable-1) {
                    break;
                }
                incRxRead();
                DidUnstuff = true;
            }
        }
    }

    if(DidUnstuff && Copy == 0) {
        // TODO(nox): Try to process new things
    }
}

static void nextFrame() {
    while(RxBuffer[RxReadIdx] != 0 && RxReadIdx != RxWriteIdx) {
        incRxRead();
    }

    if(RxBuffer[RxReadIdx] == 0 && RxReadIdx != RxWriteIdx) {
        incRxRead();
    }

    FrameReadIdx = FrameWriteIdx;
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
            SelectedFrame = incMod(SelectedFrame, arrayCount(Frames));
        }
        ShouldUpdate = false;
    }

    if(RxNewFrame) {
        unstuffBytes();
        nextFrame();
        unstuffBytes();
    }
}

#endif
