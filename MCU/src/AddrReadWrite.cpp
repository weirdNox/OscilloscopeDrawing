#if AddrReadWrite

#include <Arduino.h>
#include "external/Wire.h"
#include "external/timer.h"

#define TBF (I2C1STAT & 1)

typedef enum {
    GeneralCall = 0x00,
} address;

enum {
    LDAC = 1<<9, // RD9
};

static Timer4 Timer = {};

static void __attribute__((interrupt)) timerIsr() {
    static uint8_t State = 0;
    if(!(State & 1) && TBF) {
        ++State;
    }
    else if((State & 1) && !TBF) {
        if(State == 3) {
            LATDCLR = LDAC;
            Timer.stop();
            State = 0;
        }
        else {
            ++State;
        }
    }
    clearIntFlag(_TIMER_4_IRQ);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    Wire.begin();

    Timer.attachInterrupt(timerIsr);
    Timer.setFrequency(500000);

    TRISDCLR = LDAC;

    // NOTE(nox): General Call Read Address - 5.4.4
    LATDSET = LDAC;
    Timer.start();
    Wire.beginTransmission(GeneralCall);
    Wire.write(0x0C);
    Wire.endTransmission(false);
    Wire.requestFrom(0x60, 1);

    int Address = 0;
    if(Wire.available()) {
        int Result = Wire.read();
        Address = (0xC << 3) | ((Result >> 5) & 0x7);
        Serial.printf("Current address: 0x%X\r\n", Address);
    }

    // NOTE(nox): Write I2C Address bits - 5.6.8
    if(Address) {
        uint8_t CommandType = 0x60;
        LATDSET = LDAC;
        Timer.start();
        Wire.beginTransmission(Address);
        Wire.write(CommandType | ((Address & 0x7) << 2) | 1);
        Wire.write(CommandType | 0x2);
        Wire.write(CommandType | 0x3);
        Wire.endTransmission();
    }

    // NOTE(nox): This delay is needed because the DAC is updating the EEPROM
    delay(50);

    // NOTE(nox): General Call Read Address - 5.4.4
    LATDSET = LDAC;
    Timer.start();
    Wire.beginTransmission(GeneralCall);
    Wire.write(0x0C);
    Wire.endTransmission(false);
    Wire.requestFrom(0x61, 1);

    if(Wire.available()) {
        int Result = Wire.read();
        Address = (0xC << 3) | ((Result >> 5) & 0x7);
        Serial.printf("New address: 0x%X\r\n", Address);
    }

    Serial.println("Setup done.");
}

void loop() {
    delay(1000);
}

#endif // AddrReadWrite
