#if Plot

#include <Arduino.h>
#include "Wire.h"
#include "timer.h"

#define TBF (I2C1STAT & 1)

typedef enum {
    DacAddr = 0x60,
} address;

enum {
    LDAC = 1<<9, // RD9
};

void setup() {
    Serial.begin(115200);
    Serial.println("Setup...");

    Wire.begin();
    TRISDCLR = LDAC;

    Serial.println("Setup done.");
}

void loop() {
    delay(1000);
}

#endif
