#ifndef COMMS_H
#define COMMS_H

#include "magnet.h"
#include "motor.h"

int lastSentTime = 0;

void setupComms() { Serial.begin(115200); }

void printStatus() {
  if (millis() - lastSentTime < 500)
    return;
  Serial.print(">Motor Current:");
  Serial.println(motor.filteredCurrent, 2);

  Serial.print(">motor PID:");
  Serial.println(motor.output, 4);
  Serial.print(">currentDistance:");
  Serial.println(magnet.distanceMM, 3);
  Serial.print(">targetDistance:");
  Serial.println(motor.setpoint, 3);
  Serial.print(">pwmValue:");
  Serial.println(motor.pwmValue, 2);
  Serial.print(">Magnet Temp:");
  Serial.println(magnet.tempC, 2);

  magnet.printErrors();
  lastSentTime = millis();
}

void receiveSerial() {
  if (Serial.available() > 0) {
    String jsonString = Serial.readStringUntil('\n');
    // receive as a number
    motor.setTarget(jsonString.toFloat());
  }
}

#endif
