#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>

// Serial telemetry and commands are in calibration.h
void setupComms() { Serial.begin(115200); }

#endif
