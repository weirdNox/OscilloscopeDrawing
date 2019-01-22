#if !defined(PROTOCOL_HPP)
#define PROTOCOL_HPP

#if !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "Implemented only for little endianness!"
#endif

// NOTE(nox):
// Keeping the grid size a power of 2 with exponent < 8 let's us encode the coordinate + Z
// information in a single byte.
//
// An exponent of 6 (2^6 = 64) seems a good trade-off, because the drawings contain enough detail to be
// perceptible, without wasting many bytes when dragging and there is still space to encode the Z setting.
//
// The 12-bit code to send and our encoding will be as follows (x means ignored and V is the value we want):
//                                 MSB | LSB
// To send             |   |   |   |   |   |   | 0 | 0 | 0 | 0 | 0 | 0 |
// X           | x | Z | V | V | V | V | V | V |
// Y           | x | x | V | V | V | V | V | V |
#define inputMsb(Val) ((Val >> 2) & 0x0F)
#define inputLsb(Val) ((Val << 6) & 0xC0)
enum {
    BaudRate = 115200,
    MagicNumber = 0xA0,
    MaxPacketSize = 1<<10,
    GridSize = 1<<6,
};

// ------------------------------------------------------------------------------------------
// NOTE(nox): Animation related
enum {
    MaxFrames = 10,
    MaxActive = 300, // NOTE(nox): Limit to achieve 30 FPS
    FPS = 30,
    MinFrameTimeMs = (1000 + FPS - 1)/FPS,
    ZDisableBit = 1<<6,
};

typedef enum : u8 {
    Command_InfoLedOn,
    Command_InfoLedOff,
    Command_PowerOn,
    Command_PowerOff,
    Command_Select0,
    Command_Select1,
    Command_UpdateFrame,
    Command_UpdateFrameCount,
    CommandCount
} command;


// ------------------------------------------------------------------------------------------
// NOTE(nox): Pong related
enum {
    PaddleHeight = 2*6 + 1,
    LeftPaddleX = 3,
    RightPaddleX = GridSize-1-LeftPaddleX,
};

static const u8 PaddleMinY = PaddleHeight/2;
static const u8 PaddleMaxY = (GridSize-1) - PaddleHeight/2;

typedef enum : u8 {
    PongCmd_InfoLedOn,
    PongCmd_InfoLedOff,
    PongCmd_Update,
    PongCommandCount
} pong_command;


// ------------------------------------------------------------------------------------------
// NOTE(nox): Common to RX/TX

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

static void writeInfoLedOn(buff *Buff) {
    writeHeader(Buff, Command_InfoLedOn);
}

static void writeInfoLedOff(buff *Buff) {
    writeHeader(Buff, Command_InfoLedOff);
}

static void writePowerOn(buff *Buff) {
    writeHeader(Buff, Command_PowerOn);
}

static void writePowerOff(buff *Buff) {
    writeHeader(Buff, Command_PowerOff);
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

static void writePongUpdate(buff *Buff, u8 LeftPaddleCenter, u8 RightPaddleCenter, u8 BallX, u8 BallY) {
    writeHeader(Buff, (command)PongCmd_Update);
    writeU8(Buff, LeftPaddleCenter);
    writeU8(Buff, RightPaddleCenter);
    writeU8(Buff, BallX);
    writeU8(Buff, BallY);
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

#endif // PROTOCOL_HPP
