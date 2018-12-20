#include <Arduino.h>

#include "Wire.h"
#include "timer.h"

#define TBF (I2C1STAT & 1)

typedef enum {
    GeneralCall = 0x00,
    DacAddr = 0x61,
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
        }
        ++State;
    }
    clearIntFlag(_TIMER_4_IRQ);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    Wire.begin();

    TRISDCLR = LDAC;
    LATDSET = LDAC;

    Timer.attachInterrupt(timerIsr);
    Timer.setFrequency(500000);

    // NOTE(nox): General Call Read Address
    Timer.start();
    Wire.beginTransmission(GeneralCall);
    Wire.write(0x0C);
    Wire.endTransmission(false);
    Wire.requestFrom(0x60, 1);

    if(Wire.available()) {
        int Result = Wire.read();
        Serial.printf("%d 0x%x\n\r", Result, Result);
    }

    Serial.println("Setup done.");
}

void loop() {
    delay(1000);
}
