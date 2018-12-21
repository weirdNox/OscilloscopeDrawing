#if Plot

#include <Arduino.h>
#include "Wire.h"
#include "timer.h"

#define TBF (I2C1STAT & 1)

#define valMsb(Val) ((Val & 0xF00) >> 8)
#define valLsb(Val) ((Val & 0x0FF) >> 0)

typedef enum {
    DacAddr = 0x60,
} address;

enum {
    LDAC = 1<<9, // RD9
};

uint8_t Counter = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    Wire.begin();
    TRISDCLR = LDAC;
    LATDCLR  = LDAC;

    Serial.println("Setup done.");
}

void loop() {
    Wire.beginTransmission(DacAddr);
    Wire.write(0x0F);
    Wire.write(0xFF);
    Wire.write(0x10);
    Wire.write(0x00);
    Wire.write(0x10);
    Wire.write(0x00);
    Wire.write(0x10);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(5000);
}

#endif
