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

typedef struct {
    u8 *Data;
    u32 Size;
    u32 Write;
    u32 Read;
    u32 ReadAvailable;
} ring_buff;

u8 RxData[300];
ring_buff Rx = {RxData, arrayCount(RxData)};
u32 RxNewPackets = 0;

u8 PktData[7000];
ring_buff Packet = {PktData, arrayCount(PktData)};
bool PacketInvalid = false;

static inline u32 incMod(u32 Val, u32 Mod, u32 Increment = 1) {
    return ((Val + Increment) % Mod);
}

static void incReadUnsafe(ring_buff *Buff, u32 Increment = 1) {
    Buff->Read = incMod(Buff->Read, Buff->Size, Increment);
    --Buff->ReadAvailable;
}

static inline u8 ringReadUnsafe(ring_buff *Buff) {
    u8 Byte = Buff->Data[Buff->Read];
    incReadUnsafe(Buff);
    return Byte;
}

static inline void ringResetRead(ring_buff *Buff) {
    Buff->Read = Buff->Write;
    Buff->ReadAvailable = 0;
}

static void ringWrite(ring_buff *Buff, u8 Byte) {
    Buff->Data[Buff->Write] = Byte;
    Buff->Write = incMod(Buff->Write, Buff->Size);
    if(Buff->Write == Buff->Read) {
        Buff->Read = incMod(Buff->Read, Buff->Size);
    }
    else {
        ++Buff->ReadAvailable;
    }
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
        ++RxNewPackets;
    }
    ringWrite(&Rx, Byte);
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

static void processPacket() {
    if(Packet.ReadAvailable >= 3) {
        u8 FirstByte = Packet.Data[Packet.Read];
        u8 MagicTest = (FirstByte & 0xF0);
        u8 Command   = (FirstByte & 0x0F);
        if(MagicTest == MagicNumber && Command < CommandCount) {
            u16 Length = ((Packet.Data[Packet.Read+2] << 8) | (Packet.Data[Packet.Read+1] << 0));

            if(Packet.ReadAvailable - 3 >= Length) {
                incReadUnsafe(&Packet, 3);
                switch(Command) {
                    case Command_On: {
                        LATGSET = 1<<6;
                    } break;

                    case Command_Off: {
                        LATGCLR = 1<<6;
                    } break;

                    default: {} break;
                }
            }

            // NOTE(nox): We are done with this packet
            ringResetRead(&Packet);
        }
        else {
            // NOTE(nox): Invalid packet start!
            PacketInvalid = true;
        }
    }
}

static void unstuffBytes() {
    if(PacketInvalid) {
        // NOTE(nox): Until the next packet, all the groups in this one can be ignored!
        return;
    }

    bool DidUnstuff = false;
    u8 Code = 0xFF, Copy = 0;

    for(; Rx.ReadAvailable && Rx.Data[Rx.Read]; --Copy) {
        if(Copy) {
            ringWrite(&Packet, Rx.Data[Rx.Read]);
            incReadUnsafe(&Rx);
        }
        else {
            if(Code != 0xFF) {
                ringWrite(&Packet, 0);
            }

            Copy = Code = Rx.Data[Rx.Read];
            if(Copy-1 > Rx.ReadAvailable-1) {
                break;
            }
            incReadUnsafe(&Rx);
            DidUnstuff = true;
        }
    }

    if(Copy == 0) {
        // NOTE(nox): With classical COBS, we can ignore the zero of the last group of an encoded
        // message, because it is the "ghost zero" that is added to the end of the message before
        // encoding. We could wait for a message to arrive completely, which is marked by the delimiter,
        // and ignore the last zero.
        //
        // This way, with the delimiter at the start, we can't assume that we have the whole message yet
        // (we can't know!), so we need to add all zeros of each decoded group even if the group is the
        // last we have at the moment, because we may still be in the middle of a transmission and not at
        // the end of the message.
        //
        // On the other hand, if a message is truncated and we start to transmit another, we will know
        // immediately because the delimiter is at the beginning of the packets.
        if(Code != 0xFF) {
            ringWrite(&Packet, 0);
        }

        if(DidUnstuff) {
            processPacket();
        }
    }
}

static void nextFrame() {
    while(Rx.ReadAvailable && ringReadUnsafe(&Rx)) {}
    ringResetRead(&Packet);
    PacketInvalid = false;
    --RxNewPackets;
}

void loop() {
    if(ShouldUpdate) {
        frame *Frame = Frames + SelectedFrame;
        for(u32 I = 0; I < Frame->PointCount; ++I) {
            point *P = Frame->Points + I;
            setCoordinates(P->X, P->Y);
        }
        // TODO(nox): setCoordinates(0, 0)? Para tirar do sítio final
        // Ou então apagar o Z?
        ++FrameRepeatCount;
        if(FrameRepeatCount >= Frame->RepeatCount) {
            FrameRepeatCount = 0;
            SelectedFrame = incMod(SelectedFrame, arrayCount(Frames));
        }
        ShouldUpdate = false;
    }

    unstuffBytes();
    if(RxNewPackets) {
        nextFrame();
        unstuffBytes();
    }
}

#endif
