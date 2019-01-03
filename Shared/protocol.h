#if !defined(PROTOCOL_H)
#define PROTOCOL_H

typedef enum {
    Command_On,
    Command_Off,
    Command_Set,
    CommandCount
} command;

typedef struct {
    enum {MagicNumber = 0xA0};
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
    writeU8(Buff, (uart_buff::MagicNumber | Command));
    writeU16(Buff, PayloadLength);
}

static void writePowerOn(uart_buff *Buff) {
    writeHeader(Buff, Command_On, 0);
}

static void writePowerOff(uart_buff *Buff) {
    writeHeader(Buff, Command_Off, 0);
}

#endif // PROTOCOL_H
