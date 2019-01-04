#if !defined(PROTOCOL_H)
#define PROTOCOL_H

#if !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "Implemented only for little endianness!"
#endif

enum {
    BaudRate = 115200,
    MagicNumber = 0xA0,
};

typedef enum {
    Command_On,
    Command_Off,
    //Command_Set,
    CommandCount
} command;

typedef struct {
    u8 Data[1<<13];
    u32 Index;
} uart_buff;

static void writeU8(uart_buff *Buff, u8 Value) {
    assert(Buff->Index + sizeof(Value) <= arrayCount(Buff->Data));
    *((u8 *)(Buff->Data + Buff->Index)) = Value;
    Buff->Index += sizeof(Value);
}

static void writeU16(uart_buff *Buff, u16 Value) {
    assert(Buff->Index + sizeof(Value) <= arrayCount(Buff->Data));
    *((u16 *)(Buff->Data + Buff->Index)) = Value;
    Buff->Index += sizeof(Value);
}

static void writeHeader(uart_buff *Buff, command Command, u16 PayloadLength) {
    writeU8(Buff, (MagicNumber | Command));
    writeU16(Buff, PayloadLength);
}

static void writePowerOn(uart_buff *Buff) {
    writeHeader(Buff, Command_On, 0);
}

static void writePowerOff(uart_buff *Buff) {
    writeHeader(Buff, Command_Off, 0);
}

static void stuffBytes(uart_buff *Orig, uart_buff *Dest) {
    Dest->Index = 0;

    u8 Code = 1;
    u8 *CodeLocation = Dest->Data + Dest->Index++;
	for(u8 Read = 0; Read < Orig->Index; ++Read) {
		if(Code != 0xFF) {
			u8 Char = Orig->Data[Read];
			if (Char != 0) {
                assert(Dest->Index + 1 <= arrayCount(Dest->Data));
				Dest->Data[Dest->Index++] = Char;
				++Code;
				continue;
			}
		}

        *CodeLocation = Code;

        Code = 1;
        CodeLocation = Dest->Data + Dest->Index++;
	}

    *CodeLocation = Code;
}

#endif // PROTOCOL_H
