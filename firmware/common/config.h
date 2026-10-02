#include "structure.h"

#ifndef config_H
#define config_H

static ArmSettings arm1("00:11:22:33:44:55", 1, 0.00, 0.0, 1.0, 0.0, 0x10);

static ArmSettings *lookupTable[6] = {&arm1};

// ~~~ PINS ~~~
#ifdef ARM_PCB // ESP32-S3 SuperMini on the arm PCB (build env arm-pcb)
int MAG_SDA_PIN = 2;
int MAG_SCL_PIN = 1;

int COMMS_SDA_PIN = 4;
int COMMS_SCL_PIN = 3;

// ~~~ DRV8876 MOTOR PINS ~~~
#define PIN_CS 6 // ADC Pin connected to IPROPI / CS
#define IN1_EN 10
#define IN2_PH 9
#define MODE_PIN 12  // PMODE, latched when nSLEEP goes high
#define NSLEEP_PIN 8 // also drives VREF, the current limit reference
#define NFAULT_PIN 5 // open drain, uses the internal pull-up
#define CURRENT_SENSE_V_PER_A 1.5f // 1.5 kOhm on IPROPI, 1000 uA/A
#else
int MAG_SDA_PIN = 41;
int MAG_SCL_PIN = 40;

int COMMS_SDA_PIN = 21;
int COMMS_SCL_PIN = 22;

// ~~~ DRV8876 MOTOR PINS ~~~
#define PIN_CS 4 // ADC Pin connected to IPROPI / CS
#define IN1_EN 5
#define IN2_PH 6
#define MODE_PIN 7
#define CURRENT_SENSE_V_PER_A 2.5f
#endif
#define MOTOR_PWM_CHANNEL 0 // LEDC channel (the S3 has 8), not the pin number
#define PWM_FREQUENCY 10000
#define PWM_RESOLUTION 8
#define CURRENT_SAMPLE_COUNT 32
#define CURRENT_FILTER_ALPHA 0.15f
#define DISTANCE_AVERAGE_COUNT 5 // rolling average, adds (N-1)/2 reads of lag
#define DISTANCE_DEADBAND 0.05
#define MOTOR_POSITIVE_PWM_SCALE 1.00f
#define MOTOR_NEGATIVE_PWM_SCALE 1.00f
#define MOTOR_MIN_PWM 100.0f // lowest PWM that reliably moves the motor

// The mechanical stops are just past these, and running into one jams the
// carriage. Targets are clamped to them and the motor never drives past them.
#define TRAVEL_MIN_MM 3.0
#define TRAVEL_MAX_MM 10.0
// Within this of a limit, moving towards it, the PWM is capped so momentum and
// the sensor lag can't carry the carriage past it
#define LIMIT_SLOW_ZONE_MM 1.0
#define LIMIT_APPROACH_PWM 140.0

#endif