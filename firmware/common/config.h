#pragma once

// ~~~ PINS ~~~ (arm board, see PCB/README.md)
int MAG_SDA_PIN = 2;
int MAG_SCL_PIN = 3;

int COMMS_SDA_PIN = 21;
int COMMS_SCL_PIN = 22;

// ~~~ DRV8876 MOTOR PINS ~~~
// (PMODE tied to GND: PH/EN mode)
#define PIN_IPROPI 1
#define M_PWM_PIN 13
#define M_DIR_PIN 12
#define PIN_nSLEEP 11
#define PIN_nFAULT 10

// ~~~ CURRENT FEEDBACK ~~~
#define CURRENT_SENSE_V_PER_A 3.3f // R1 = 3.3k on IPROPI
#define CURRENT_SAMPLE_COUNT 32
#define CURRENT_FILTER_ALPHA 0.15f

// ~~~ PID & CONTROL ~~~
#define DISTANCE_AVERAGE_COUNT 3 // rolling median, adds (N-1)/2 reads of lag
// DISTANCE_DEADBAND and each board's min PWM and gains are in defaults.h

// ledcWrite turns 255 (8-bit) into "full on", which the S3 outputs as off
#define MOTOR_MAX_PWM 254.0
#define MAGNET_TIMEOUT_MS 100

#define TRAVEL_MIN_MM 2.5
#define TRAVEL_MAX_MM 10.5