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

static Timer2 FrameTimer = {};
static Timer4 ZTimer = {};

static bool ShouldUpdate = false;
static u32 SelectedFrame = 0;
static u32 FrameRepeatCount = 0;

static rx_buff Rx;
static buff Pkt;
static bool SkipPacket;

static void nextFrame() {
    while(Rx.Read != Rx.Write && Rx.Data[Rx.Read]) {
        Rx.Read = (Rx.Read + 1) & Rx.Mask;
    }

    if(Rx.Read != Rx.Write && Rx.Data[Rx.Read] == 0) {
        Rx.Read = (Rx.Read + 1) & Rx.Mask;
        --Rx.NewPacketCount;
        resetBuff(&Pkt);
        SkipPacket = false;
    }
}

static void decodeRx() {
    if(SkipPacket) {
        nextFrame();
        if(SkipPacket) {
            return;
        }
    }

    // NOTE(nox): With classical COBS, we ignore the zero of the last group of an encoded message,
    // because it is the "ghost zero" that is added to the end of the message before encoding. We just
    // wait for a message to arrive completely, which is marked by the delimiter, and ignore the last
    // zero.
    //
    // However, with the delimiter at the start, we can't assume that we have the whole message yet (we
    // can't know!), so we need to add all zeros of each decoded group even if the group is the last we
    // have at the moment, because we may still be in the middle of a transmission and not at the end of
    // the message.
    //
    // On the other hand, if a message is truncated and we start to transmit another, we will know
    // immediately because the delimiter is at the beginning of the packets.

    bool DidUnstuff = false;
    u8 Code = 0xFF, Copy = 0;
    for(;; --Copy) {
        u8 Byte = Rx.Data[Rx.Read];
        if(Copy == 0) {
            if(Code != 0xFF) {
                Pkt.Data[Pkt.Write++] = 0;
            }

            Copy = Code = Byte;
            if(Code == 0 || Copy > ((Rx.Write - Rx.Read) & Rx.Mask)) {
                break;
            }

            Rx.Read = (Rx.Read + 1) & Rx.Mask;
            DidUnstuff = true;
        }
        else if(Byte) {
            Pkt.Data[Pkt.Write++] = Rx.Data[Rx.Read];
            Rx.Read = (Rx.Read + 1) & Rx.Mask;
        }
        else {
            // NOTE(nox): Encoded message ends too soon! We have encountered a delimeter (0) while we
            // should still be copying
            return;
        }
    }

    if(DidUnstuff && Pkt.Write >= 3) {
        u8 FirstByte = readU8(&Pkt);
        u8 MagicTest =             (FirstByte & 0xF0);
        command Command = (command)(FirstByte & 0x0F);
        if(MagicTest == MagicNumber) {
            u16 Length = readU16(&Pkt, 1);
            if(Pkt.Write - 3 >= Length) {
                Pkt.Read += 3;

                switch(Command) {
                    case Command_On: {
                        LATGSET = 1<<6;
                    } break;

                    case Command_Off: {
                        LATGCLR = 1<<6;
                    } break;

                    case Command_Toggle: {
                        LATGINV = 1<<6;
                    } break;

                    default: {} break;
                }

                // NOTE(nox): We are done with this packet
                SkipPacket = true;
            }
        }
        else {
            // NOTE(nox): Invalid packet start!
            SkipPacket = true;
        }
    }
}

// NOTE(nox): X and Y are in the range [0, 4096[
static void setCoordinates(u16 X, u16 Y) {
    LATDCLR = (X & ZDisableBit) ? ZPin : 0;
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

static void __USER_ISR setUpdateFlag() {
    ShouldUpdate = true;
    clearIntFlag(_TIMER_2_IRQ);
}

static void __USER_ISR enableZ() {
    LATDSET = ZPin;
    ZTimer.stop();
    clearIntFlag(_TIMER_4_IRQ);
}

static void __USER_ISR uartRx() {
    u8 Byte = U1RXREG;
    u32 NextWrite = (Rx.Write + 1) & Rx.Mask;

    if(NextWrite != Rx.Read) {
        Rx.Data[Rx.Write] = Byte;
        Rx.Write = NextWrite;
        if(Byte == 0) {
            Rx.NewPacketCount++;
        }
    } // NOTE(nox): In the case of a buffer overflow, the _new_ byte is dropped

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

    // ----------------------------------------------------------------------------------------------------
    // TODO(nox): Remove this after testing with the actual MCP4728
    TRISGCLR = 1<<6;
    LATGCLR  = 1<<6;
    // ----------------------------------------------------------------------------------------------------

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

void loop() {
    if(ShouldUpdate) {
        frame *Frame = Frames + SelectedFrame;
        for(u32 I = 0; I < Frame->PointCount; ++I) {
            point *P = Frame->Points + I;
            setCoordinates(P->X, P->Y);
        }
        // TODO(nox): setCoordinates(0, 0)? Ou então apagar o Z? Para tirar do sítio final.
        ++FrameRepeatCount;
        if(FrameRepeatCount >= Frame->RepeatCount) {
            FrameRepeatCount = 0;
            SelectedFrame = incMod(SelectedFrame, arrayCount(Frames));
        }
        ShouldUpdate = false;
    }

    decodeRx();
    while(Rx.NewPacketCount) {
        nextFrame();
        decodeRx();
    }
}

#endif
