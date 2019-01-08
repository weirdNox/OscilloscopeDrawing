#if !defined(PROTOCOL_H)
#define PROTOCOL_H

#if !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "Implemented only for little endianness!"
#endif

// ------------------------------------------------------------------------------------------
// NOTE(nox): Common to RX/TX
enum {
    BaudRate = 115200,
    MagicNumber = 0xA0,
    MaxPacketSize = 1<<10,
};

typedef enum : u8 {
    Command_On,
    Command_Off,
    Command_Select0,
    Command_Select1,
    Command_UpdateFrame,
    Command_UpdateFrameCount,
    CommandCount
} command;

typedef struct {
    u32 Read;
    u32 Write;
    u8 Data[MaxPacketSize];
} buff;

static inline void resetBuff(buff *Buff) {
    Buff->Read  = 0;
    Buff->Write = 0;
}


// ------------------------------------------------------------------------------------------
// NOTE(nox): RX related
typedef struct {
    // NOTE(nox):
    // - Size must be _power of two_ and must be enough so that it is able to hold a continuous stream of
    //   data while we plot the points.
    //   In the worst case, with 300 points @ 120us/point, it takes 0.036 seconds. If we are transmitting
    //   @ 115200 baud, it transmits 4150 bits = 518 bytes which possibly may be managed with 512 bytes
    //   of buffering.
    //
    //  - Both Write and NewPacketCount need to be _volatile_ because they are modified in the ISR
    u8 Data[1<<10];
    u32 Read;
    volatile u32 Write;
    volatile u32 NewPacketCount;
    enum { Mask = (sizeof(Data) - 1) };
} rx_buff;

static inline u8 readU8(buff *Buff) {
    u8 Result = (u8)(Buff->Data[Buff->Read]);
    Buff->Read += sizeof(Result);
    return Result;
}

static inline u16 readU16(buff *Buff) {
    u16 Result = (u16)((Buff->Data[Buff->Read+1] << 8) |
                       (Buff->Data[Buff->Read+0] << 0));
    Buff->Read += sizeof(Result);
    return Result;
}

static inline u8 readU8NoAdv(buff *Buff, u32 Offset = 0) {
    return (u8)(Buff->Data[Buff->Read+Offset]);
}

static inline u16 readU16NoAdv(buff *Buff, u32 Offset = 0) {
    return (u16)((Buff->Data[Buff->Read+Offset+1] << 8) |
                 (Buff->Data[Buff->Read+Offset+0] << 0));
}


// ------------------------------------------------------------------------------------------
// NOTE(nox): TX related
static void writeU8(buff *Buff, u8 Value) {
    assert(Buff->Write + sizeof(Value) <= arrayCount(Buff->Data));
    *((u8 *)(Buff->Data + Buff->Write)) = Value;
    Buff->Write += sizeof(Value);
}

static void writeU16(buff *Buff, u16 Value) {
    assert(Buff->Write + sizeof(Value) <= arrayCount(Buff->Data));
    *((u16 *)(Buff->Data + Buff->Write)) = Value;
    Buff->Write += sizeof(Value);
}

static void writeHeader(buff *Buff, command Command) {
    writeU8(Buff, (MagicNumber | Command));
    writeU16(Buff, 0); // NOTE(nox): Placeholder for length
}

static void writePowerOn(buff *Buff) {
    writeHeader(Buff, Command_On);
}

static void writePowerOff(buff *Buff) {
    writeHeader(Buff, Command_Off);
}

static void writeSelectAnim(buff *Buff, int Anim) {
    if(Anim) {
        writeHeader(Buff, Command_Select1);
    }
    else {
        writeHeader(Buff, Command_Select0);
    }
}

static void writeUpdateFrameCount(buff *Buff, u8 NewFrameCount) {
    writeHeader(Buff, Command_UpdateFrameCount);
    writeU8(Buff, NewFrameCount);
}

static void stuffBytes(buff *Orig, buff *Dest) {
    Dest->Write = 0;

    u8 Code = 1;
    u8 *CodeLocation = Dest->Data + Dest->Write++;
	for(u32 Read = 0; Read < Orig->Write;) {
		if(Code != 0xFF) {
			u8 Char = Orig->Data[Read++];
			if (Char != 0) {
                assert(Dest->Write + 1 <= arrayCount(Dest->Data));
				Dest->Data[Dest->Write++] = Char;
				++Code;
				continue;
			}
		}

        *CodeLocation = Code;

        Code = 1;
        CodeLocation = Dest->Data + Dest->Write++;
	}

    *CodeLocation = Code;
}

static inline void finalizePacket(buff *Buff, buff *Dest) {
    // NOTE(nox): Update packet length
    *((u16 *)(Buff->Data + 1)) = Buff->Write-3;
    stuffBytes(Buff, Dest);
}

#endif // PROTOCOL_H
