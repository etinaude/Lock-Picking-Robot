#include "structure.h"

#ifndef config_H
#define config_H

static ArmSettings arm1("00:11:22:33:44:55", 1, 0.00, 0.0, 1.0, 0.0, 0x10);

static ArmSettings *lookupTable[6] = {&arm1};

// ~~~ PINS ~~~
int MAG_SDA_PIN = 41;
int MAG_SCL_PIN = 40;

int COMMS_SDA_PIN = 21;
int COMMS_SCL_PIN = 22;

// ~~~ DRV8876 MOTOR PINS ~~~
#define PIN_CS 4 // ADC Pin connected to IPROPI / CS
#define IN1_EN 5
#define IN2_PH 6
#define MODE_PIN 7
#define PWM_FREQUENCY 5000
#define PWM_RESOLUTION 8
#define CURRENT_SAMPLE_COUNT 32
#define CURRENT_FILTER_ALPHA 0.15f
#define DISTANCE_DEADBAND 0.05

#endif