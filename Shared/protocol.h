#if !defined(PROTOCOL_H)
#define PROTOCOL_H

#if !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "Implemented only for little endianness!"
#endif

enum {
    BaudRate = 115200,
    MagicNumber = 0xA0,
    MaxPacketSize = 1<<10,
    ZDisableBit = 1<<12,
};

typedef enum : u8 {
    Command_On,
    Command_Off,
    Command_Toggle,
    CommandCount
} command;

typedef struct {
    u32 Read;
    u32 Write;
    u8 Data[MaxPacketSize];
} buff;


// ------------------------------------------------------------------------------------------
// NOTE(nox): Encode related
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

static void writeHeader(buff *Buff, command Command, u16 PayloadLength) {
    writeU8(Buff, (MagicNumber | Command));
    writeU16(Buff, PayloadLength);
}

static void writePowerOn(buff *Buff) {
    writeHeader(Buff, Command_On, 0);
}

static void writePowerOff(buff *Buff) {
    writeHeader(Buff, Command_Off, 0);
}

static void writePowerToggle(buff *Buff) {
    writeHeader(Buff, Command_Toggle, 0);
}

static void stuffBytes(buff *Orig, buff *Dest) {
    Dest->Write = 0;

    u8 Code = 1;
    u8 *CodeLocation = Dest->Data + Dest->Write++;
	for(u32 Read = 0; Read < Orig->Write; ++Read) {
		if(Code != 0xFF) {
			u8 Char = Orig->Data[Read];
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

// ------------------------------------------------------------------------------------------
// NOTE(nox): Decode related
typedef struct {
    // NOTE(nox):
    // - Size must be _power of two_ and must be enough so that it is able to hold a continuous stream of
    //   data while we plot the points.
    //   In the worst case, with 300 points @ 120us/point, it takes 0.036 seconds. If we are transmitting
    //   @ 115200 baud, it transmits 4150 bits = 518 bytes which possibly may be managed with 512 bytes
    //   of buffering.
    //
    //  - Both Write and NewPacketCount need to be volatile because they are modified in the ISR
    //
    //  TODO(nox): Check if 512 bytes is enough to hold the streaming data
    //
    u8 Data[1<<9];
    u32 Read;
    volatile u32 Write;
    volatile u32 NewPacketCount;
    enum { RxMask = (sizeof(Data) - 1) };
} rx_buff;

#define PROCESS_COMMANDS_FN(Name) void Name(command Command, buff *Payload, u16 Length)
typedef PROCESS_COMMANDS_FN(process_commands_fn);
process_commands_fn *ProcessCommandsFn;

static inline u8 readU8(buff *Buff, u32 Offset = 0) {
    return (u8)(Buff->Data[Buff->Read+Offset]);
}

static inline u16 readU16(buff *Buff, u32 Offset = 0) {
    return (u16)((Buff->Data[Buff->Read+Offset+1] << 8) |
                 (Buff->Data[Buff->Read+Offset+0] << 8));
}

#endif // PROTOCOL_H