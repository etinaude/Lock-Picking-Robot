#include "structure.h"

#ifndef config_H
#define config_H

static ArmSettings arm1("00:11:22:33:44:55", 0.1, 0.01, 0.001, 1.0, 0.0, 0x10);

static ArmSettings *lookupTable[6] = {&arm1};

// ~~~ PINS ~~~
int PWM_PIN = 3;
int MAG_SDA_PIN = 21;
int MAG_SCL_PIN = 22;

int COMMS_SDA_PIN = 21;
int COMMS_SCL_PIN = 22;

#endif