#include "comms.h"
#include "motor.h"
#include <Arduino.h>

void setup() {
  setupComms();
  setupMotor();
  setupMagnet();
}

void loop() {
  handlePID();
  sendSerial();
  receiveSerial();
}