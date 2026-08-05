#include "comms.h"
#include "motor.h"
#include <Arduino.h>

void setup() {
  setupComms();
  setupMotor();
  setupMagnet();
}

void loop() {
  readMagnet();
  handlePID();
  sendSerial();
  receiveSerial();
  delay(50);
}