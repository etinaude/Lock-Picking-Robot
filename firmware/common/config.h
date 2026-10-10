#include "structure.h"

#ifndef config_H
#define config_H

// ~~~ PINS ~~~
int MAG_SDA_PIN = 41;
int MAG_SCL_PIN = 40;

int COMMS_SDA_PIN = 21;
int COMMS_SCL_PIN = 22;

// ~~~ DRV8876 MOTOR PINS ~~~
#define PIN_CS 4
#define IN1_EN 5
#define IN2_PH 6
#define MODE_PIN 7

// ~~~ CURRENT FEEDBACK ~~~
#define CURRENT_SENSE_V_PER_A 2.5f
#define CURRENT_SAMPLE_COUNT 32
#define CURRENT_FILTER_ALPHA 0.15f

// ~~~ PID & CONTROL ~~~
#define DISTANCE_AVERAGE_COUNT 3 // rolling median, adds (N-1)/2 reads of lag
#define DISTANCE_DEADBAND 0.01
#define MOTOR_MIN_PWM 20.0f
// ledcWrite turns 255 (8-bit) into "full on", which the S3 outputs as off
#define MOTOR_MAX_PWM 254.0
#define MAGNET_TIMEOUT_MS 100

#define TRAVEL_MIN_MM 2.5
#define TRAVEL_MAX_MM 10.5

#endif