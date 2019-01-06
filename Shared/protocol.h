#if !defined(PROTOCOL_H)
#define PROTOCOL_H

#if !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "Implemented only for little endianness!"
#endif

enum {
    BaudRate = 115200,
    MagicNumber = 0xA0,
    MaxPacketSize = 1<<10,
};

typedef enum {
    Command_On,
    Command_Off,
    Command_Toggle,
    CommandCount
} command;

// ------------------------------------------------------------------------------------------
// NOTE(nox): Encode related
typedef struct {
    u8 Data[MaxPacketSize];
    u32 Index;
} send_buff;

static void writeU8(send_buff *Buff, u8 Value) {
    assert(Buff->Index + sizeof(Value) <= arrayCount(Buff->Data));
    *((u8 *)(Buff->Data + Buff->Index)) = Value;
    Buff->Index += sizeof(Value);
}

static void writeU16(send_buff *Buff, u16 Value) {
    assert(Buff->Index + sizeof(Value) <= arrayCount(Buff->Data));
    *((u16 *)(Buff->Data + Buff->Index)) = Value;
    Buff->Index += sizeof(Value);
}

static void writeHeader(send_buff *Buff, command Command, u16 PayloadLength) {
    writeU8(Buff, (MagicNumber | Command));
    writeU16(Buff, PayloadLength);
}

static void writePowerOn(send_buff *Buff) {
    writeHeader(Buff, Command_On, 0);
}

static void writePowerOff(send_buff *Buff) {
    writeHeader(Buff, Command_Off, 0);
}

static void writePowerToggle(send_buff *Buff) {
    writeHeader(Buff, Command_Toggle, 0);
}

static void stuffBytes(send_buff *Orig, send_buff *Dest) {
    Dest->Index = 0;

    u8 Code = 1;
    u8 *CodeLocation = Dest->Data + Dest->Index++;
	for(u32 Read = 0; Read < Orig->Index; ++Read) {
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

// ------------------------------------------------------------------------------------------
// NOTE(nox): Decode related
typedef struct {
    u8 *Data;
    u32 Size;
    u32 Write;
    u32 Read;
    u32 ReadAvailable;
} ring_buff;

typedef struct {
    ring_buff Rx;
    ring_buff Pkt;
    u32 RxNewPackets;
    bool SkipPacket;
} decode_ctx;

#define PROCESS_COMMANDS_FN(Name) void Name(u8 Command, ring_buff *Payload, u16 Length)
typedef PROCESS_COMMANDS_FN(process_commands_fn);
process_commands_fn *ProcessCommandsFn;

static void ringReadIncUnsafe(ring_buff *Buff, u32 Increment = 1) {
    Buff->Read = incMod(Buff->Read, Buff->Size, Increment);
    Buff->ReadAvailable -= Increment;
}

static inline u8 ringReadUnsafe(ring_buff *Buff) {
    u8 Byte = Buff->Data[Buff->Read];
    ringReadIncUnsafe(Buff);
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

static void processPacket(decode_ctx *Ctx) {
    ring_buff *Pkt = &Ctx->Pkt;
    if(Pkt->ReadAvailable >= 3) {
        u8 FirstByte = Pkt->Data[Pkt->Read];
        u8 MagicTest = (FirstByte & 0xF0);
        u8 Command   = (FirstByte & 0x0F);
        if(MagicTest == MagicNumber) {
            u16 Length = ((Pkt->Data[Pkt->Read+2] << 8) | (Pkt->Data[Pkt->Read+1] << 0));

            if(Pkt->ReadAvailable - 3 >= Length) {
                ringReadIncUnsafe(Pkt, 3);
                ProcessCommandsFn(Command, Pkt, Length);

                // NOTE(nox): We are done with this packet
                Ctx->SkipPacket = true;
            }
        }
        else {
            // NOTE(nox): Invalid packet start!
            Ctx->SkipPacket = true;
        }
    }
}

static void decodeRx(decode_ctx *Ctx) {
    if(Ctx->SkipPacket) {
        return;
    }

    bool DidUnstuff = false;
    ring_buff *Rx = &Ctx->Rx;
    ring_buff *Pkt = &Ctx->Pkt;
    u8 Code = 0xFF, Copy = 0;

    for(; Rx->ReadAvailable && Rx->Data[Rx->Read]; --Copy) {
        if(Copy) {
            ringWrite(Pkt, Rx->Data[Rx->Read]);
            ringReadIncUnsafe(Rx);
        }
        else {
            if(Code != 0xFF) {
                ringWrite(Pkt, 0);
            }

            Copy = Code = Rx->Data[Rx->Read];
            if(Copy-1 > Rx->ReadAvailable-1) {
                break;
            }
            ringReadIncUnsafe(Rx);
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
            ringWrite(Pkt, 0);
        }

        if(DidUnstuff) {
            processPacket(Ctx);
        }
    }
}

static void nextFrame(decode_ctx *Ctx) {
    while(Ctx->Rx.ReadAvailable && ringReadUnsafe(&Ctx->Rx)) {}
    ringResetRead(&Ctx->Pkt);
    Ctx->SkipPacket = false;
    --Ctx->RxNewPackets;
}

#endif // PROTOCOL_H
