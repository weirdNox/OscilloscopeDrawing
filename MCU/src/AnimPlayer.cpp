#if AnimPlayer

#include <Arduino.h>
#include "external/Wire.h"
#include "external/timer.h"

#define assert(...)
#include <common.h>
#include <protocol.hpp>

enum {
    DacAddr = 0x60,
    LDAC = 1<<9,    // RD9
    ZPin = 1<<2,    // RD2
    InfoLed = 1<<6, // RG2
    MaxPointsPerFrame = 300,
    FPB = 80000000,
};

#include "Animations.h"

static Timer2 FrameTimer = {};
static Timer4 ZTimer = {};

static volatile bool ShouldUpdate = false;
static u32 SelectedAnimation = 0;
static u32 SelectedFrame = 0;
static u32 FrameRepeatCount = 0;
static bool SetTo0 = false;

static rx_buff Rx;
static buff Pkt;
static bool SkipPacket;

static void selectFrame(u8 FrameIdx) {
    animation *Animation = Animations + SelectedAnimation;
    if(FrameIdx < Animation->FrameCount) {
        FrameRepeatCount = 0;
        SelectedFrame = FrameIdx;
        u16 Fps = max(Animation->Frames[SelectedFrame].Fps, MinFps);
        FrameTimer.setFrequency(Fps);
    }
}

static inline void selectAnim(int Anim) {
    if(Anim) {
        SelectedAnimation = 1;
    }
    else {
        SelectedAnimation = 0;
    }
    selectFrame(0);
}

// NOTE(nox): X and Y are in the range [0, 64[, except when X has the Z bit set.
static void setCoordinates(u8 X, u8 Y) {
    LATDSET = LDAC;

    // NOTE(nox): Multi-Write command - 5.6.2
    u8 Data[] = {(0x40 | (0 << 1) | 1), (0x90 | inputMsb(X)), inputLsb(X),  // Output A
                 (0x40 | (1 << 1) | 1), (0x90 | inputMsb(Y)), inputLsb(Y)}; // Output B
    Wire.beginTransmission(DacAddr);
    Wire.write(Data, arrayCount(Data));
    Wire.endTransmission();

    LATDCLR = (X & ZDisableBit) ? ZPin : 0;
    ZTimer.start();

    LATDCLR = LDAC; // NOTE(nox): Active both outputs at the same time
}

static void powerOffOutputs() {
    // NOTE(nox): Select power-down bits - 5.6.6
    // PD1 = 1, PD0 = 0 -> 100kΩ to ground
    enum {Cmd = 0xA0};
    u8 Data[] = {(Cmd | 0x0A), (0xA0)};
    Wire.beginTransmission(DacAddr);
    Wire.write(Data, arrayCount(Data));
    Wire.endTransmission();
}

static void nextPacket() {
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
        nextPacket();
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
            Pkt.Data[Pkt.Write++] = Byte;
            Rx.Read = (Rx.Read + 1) & Rx.Mask;
        }
        else {
            // NOTE(nox): Encoded message ends too soon! We have encountered a delimeter (0) while we
            // should still be copying
            return;
        }
    }

    if(DidUnstuff && Pkt.Write >= (1+2)) {
        u8 FirstByte = readU8NoAdv(&Pkt);
        u8 MagicTest =             (FirstByte & 0xF0);
        command Command = (command)(FirstByte & 0x0F);
        if(MagicTest == MagicNumber) {
            u16 Length = readU16NoAdv(&Pkt, 1);
            if(Pkt.Write - 3 >= Length) {
                Pkt.Read += 3;

                switch(Command) {
                    case Command_InfoLedOn: {
                        LATGSET = InfoLed;
                    } break;

                    case Command_InfoLedOff: {
                        LATGCLR = InfoLed;
                    } break;

                    case Command_PowerOn: {
                        selectFrame(0);
                        FrameTimer.start();
                        LATDSET = ZPin;
                    } break;

                    case Command_PowerOff: {
                        FrameTimer.stop();
                        ZTimer.stop();
                        ShouldUpdate = false;
                        powerOffOutputs();
                        LATDCLR = ZPin;
                    } break;

                    case Command_Select0: {
                        selectAnim(0);
                    } break;

                    case Command_Select1: {
                        selectAnim(1);
                    } break;

                    case Command_UpdateFrame: {
                        enum { CmdHeaderSize = 1+2+2+2 };

                        if(Length < CmdHeaderSize) {
                            break;
                        }

                        u8 FrameIdx = readU8(&Pkt);
                        u16 Fps = readU16(&Pkt);
                        u16 RepeatCount = readU16(&Pkt);
                        u16 PointCount = readU16(&Pkt);

                        if((Length-CmdHeaderSize < 2*PointCount ||
                            FrameIdx > MaxFrames || PointCount > MaxPointsPerFrame)) {
                            break;
                        }

                        frame *Frame = Animations[SelectedAnimation].Frames + FrameIdx;
                        Frame->Fps = max(Fps, MinFps);
                        Frame->RepeatCount = RepeatCount;
                        Frame->PointCount  = PointCount;
                        for(u16 I = 0; I < PointCount; ++I) {
                            point *Point = Frame->Points + I;
                            Point->X = readU8(&Pkt);
                            Point->Y = readU8(&Pkt);
                        }

                        if(SelectedFrame == FrameIdx) {
                            selectFrame(FrameIdx);
                        }
                    } break;

                    case Command_UpdateFrameCount: {
                        u8 FrameCount = readU8(&Pkt);
                        FrameCount = clamp(1, FrameCount, MaxFrames);
                        Animations[SelectedAnimation].FrameCount = FrameCount;

                        selectFrame(0);
                    } break;

                    case Command_SetTo0: {
                        SetTo0 = true;
                    } break;

                    case Command_DontSetTo0: {
                        SetTo0 = false;
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
    }
    else {
        // NOTE(nox): In the case of a buffer overflow, the _new_ byte is dropped. The information LED
        // will light up so we know if it ever happens.
        LATGSET = InfoLed;
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
    TRISGCLR = InfoLed;
    LATGCLR  = InfoLed;

    Wire.begin();
    u32 Clock = Wire.setClock(1000000);

    // NOTE(nox): Sequential write command (A -> D) - 5.6.3
    {
        Wire.beginTransmission(DacAddr);
        Wire.write(0x50);

        // NOTE(nox): VRef = 1 (internal voltage reference), Gx = 1 (2x gain)
        u8 ActiveData[] = {0x90, 0x00};
        Wire.write(ActiveData, 2);
        Wire.write(ActiveData, 2);

        // NOTE(nox): PD1 = 1, PD0 = 0 -> 100kΩ to ground
        u8 DisabledData[] = {0x40, 0x00};
        Wire.write(DisabledData, 2);
        Wire.write(DisabledData, 2);

        Wire.endTransmission();
    }
    delay(50);

    selectAnim(0);
    FrameTimer.attachInterrupt(setUpdateFlag);
    FrameTimer.start();

    // NOTE(nox): This prevents the transitions from being visible in the XYZ mode, and needs to wait at
    // least the settling time, 6.5us. Giving it some margin, we chose 10us of period.
    ZTimer.setFrequency(100000);
    ZTimer.attachInterrupt(enableZ);
}

void loop() {
    if(ShouldUpdate) {
        animation *Anim = Animations + SelectedAnimation;
        frame *Frame = Anim->Frames + SelectedFrame;
        for(u32 I = 0; I < Frame->PointCount; ++I) {
            point *P = Frame->Points + I;
            setCoordinates(P->X, P->Y);
        }
        if(SetTo0) {
            setCoordinates(0, 0);
        }

        ++FrameRepeatCount;
        if(FrameRepeatCount >= Frame->RepeatCount) {
            selectFrame((SelectedFrame + 1 >= Anim->FrameCount) ? 0 : SelectedFrame + 1);
        }
        ShouldUpdate = false;
    }

    decodeRx();
    while(Rx.NewPacketCount) {
        nextPacket();
        decodeRx();
    }
}

#endif
