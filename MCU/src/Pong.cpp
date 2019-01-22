#if Pong
#include <Arduino.h>
#include "external/Wire.h"
#include "external/timer.h"

#define assert(...)
#include <common.h>
#include <protocol.hpp>

enum {
    DacAddr = 0x60,
    LDAC = 1<<9,    // RD9
    InfoLed = 1<<6, // RG2
    MaxPointsPerFrame = 300,
    FPB = 80000000,
};

static Timer2 FrameTimer = {};

static volatile bool ShouldUpdate = false;
static u8 LeftPaddleCenter;
static u8 RightPaddleCenter;
static u8 BallX;
static u8 BallY;
static u8 LeftScore;
static u8 RightScore;

typedef struct {
    u8 X, Y;
} point;
typedef struct {
    u32 PointCount;
    point Points[13];
} number;
static const number Numbers[] = {
    {
        12,
        {
            {31, 62}, {32, 62}, {33, 62}, {33, 61}, {33, 60}, {33, 59}, {33, 58},
            {32, 58}, {31, 58}, {31, 59}, {31, 60}, {31, 61},
        }
    },
    {
        5,
        {
            {32, 62}, {32, 61}, {32, 60}, {32, 59}, {32, 58},
        }
    },
    {
        11,
        {
            {31, 62}, {32, 62}, {33, 62}, {33, 61}, {33, 60}, {32, 60}, {31, 60},
            {31, 59}, {31, 58}, {32, 58}, {33, 58},
        }
    },
    {
        10,
        {
            {31, 62}, {32, 62}, {33, 62}, {33, 61}, {33, 60}, {32, 60}, {33, 59},
            {33, 58}, {32, 58}, {31, 58},
        }
    },
    {
        9,
        {
            {31, 62}, {31, 61}, {31, 60}, {32, 60}, {33, 60}, {33, 61}, {33, 62},
            {33, 59}, {33, 58},
        }
    },
    {
        11,
        {
            {33, 62}, {32, 62}, {31, 62}, {31, 61}, {31, 60}, {32, 60}, {33, 60},
            {33, 59}, {33, 58}, {32, 58}, {31, 58},
        }
    },
    {
        12,
        {
            {33, 62}, {32, 62}, {31, 62}, {31, 61}, {31, 60}, {31, 59}, {31, 58},
            {32, 58}, {33, 58}, {33, 59}, {33, 60}, {32, 60},
        }
    },
    {
        7,
        {
            {31, 62}, {32, 62}, {33, 62}, {33, 61}, {33, 60}, {33, 59}, {33, 58},
        }
    },
    {
        13,
        {
            {33, 62}, {32, 62}, {31, 62}, {31, 61}, {31, 60}, {32, 60}, {33, 60},
            {33, 61}, {33, 59}, {33, 58}, {32, 58}, {31, 58}, {31, 59},
        }
    },
    {
        12,
        {
            {31, 58}, {32, 58}, {33, 58}, {33, 59}, {33, 60}, {32, 60}, {31, 60},
            {31, 61}, {31, 62}, {32, 62}, {33, 62}, {33, 61},
        }
    }
};

static rx_buff Rx;
static buff Pkt;
static bool SkipPacket;

// NOTE(nox): X and Y are in the range [0, 64[, except when X has the Z bit set.
static void setCoordinates(u8 X, u8 Y) {
    LATDSET = LDAC;

    // NOTE(nox): Multi-Write command - 5.6.2
    u8 Data[] = {(0x40 | (0 << 1) | 1), (0x90 | inputMsb(X)), inputLsb(X),  // Output A
                 (0x40 | (1 << 1) | 1), (0x90 | inputMsb(Y)), inputLsb(Y)}; // Output B
    Wire.beginTransmission(DacAddr);
    Wire.write(Data, arrayCount(Data));
    Wire.endTransmission();

    LATDCLR = LDAC; // NOTE(nox): Active both outputs at the same time
}

static void drawNumber(u8 I, u8 Offset) {
    if(I >= 10) {
        I = 0;
    }

    const number *Number = Numbers+I;
    for(u32 Current = 0; Current < Number->PointCount; ++Current) {
        const point *Point = Number->Points + Current;
        setCoordinates(Point->X+Offset, Point->Y);
    }
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
        pong_command Command = (pong_command)(FirstByte & 0x0F);
        if(MagicTest == MagicNumber) {
            u16 Length = readU16NoAdv(&Pkt, 1);
            if(Pkt.Write - 3 >= Length) {
                Pkt.Read += 3;

                switch(Command) {
                    case PongCmd_InfoLedOn: {
                        LATGSET = InfoLed;
                    } break;

                    case PongCmd_InfoLedOff: {
                        LATGCLR = InfoLed;
                    } break;

                    case PongCmd_Update: {
                        enum { CmdSize = 4 };
                        if(Length < CmdSize) {
                            break;
                        }
                        LeftPaddleCenter = readU8(&Pkt);
                        RightPaddleCenter = readU8(&Pkt);
                        BallX = readU8(&Pkt);
                        BallY = readU8(&Pkt);
                    } break;

                    case PongCmd_SetScore: {
                        enum { CmdSize = 2 };
                        if(Length < CmdSize) {
                            break;
                        }
                        LeftScore = readU8(&Pkt);
                        RightScore = readU8(&Pkt);
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

    TRISDCLR = LDAC;
    LATDSET  = LDAC;
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

    // NOTE(nox): 60 FPS
    FrameTimer.setFrequency(60);
    FrameTimer.attachInterrupt(setUpdateFlag);
    FrameTimer.start();
}

void loop() {
    if(ShouldUpdate) {
        for(s8 I = -PaddleHeight/2; I <= PaddleHeight/2; ++I) {
            setCoordinates(LeftPaddleX, LeftPaddleCenter+I);
        }
        for(s8 I = -PaddleHeight/2; I <= PaddleHeight/2; ++I) {
            setCoordinates(RightPaddleX, RightPaddleCenter+I);
        }
        drawNumber(LeftScore, -5);
        setCoordinates(31, 60);
        setCoordinates(32, 60);
        drawNumber(RightScore, 4);
        setCoordinates(BallX, BallY);
        ShouldUpdate = false;
    }

    decodeRx();
    while(Rx.NewPacketCount) {
        nextPacket();
        decodeRx();
    }
}

#endif
