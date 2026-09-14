#ifndef COMMS_H
#define COMMS_H

#include "magnet.h"
#include "motor.h"
#include "state.h"

void setupComms() { Serial.begin(115200); }

void printStatus() {
  if (millis() - lastSentTime < 500)
    return;
  Serial.print(">Motor Current:");
  Serial.println(filteredCurrent, 2);

  Serial.print(">motor PID:");
  Serial.println(motorPIDOut, 4);
  Serial.print(">currentDistance:");
  Serial.println(currentDistance, 2);
  Serial.print(">targetDistance:");
  Serial.println(targetDistance, 2);
  Serial.print(">pwmValue:");
  Serial.println(pwmValue, 2);

  printMagnet();
  lastSentTime = millis();
}

void receiveSerial() {
  if (Serial.available() > 0) {
    String jsonString = Serial.readStringUntil('\n');
    // receive as a number
    setTargetDistance(jsonString.toFloat());
  }
}

#endif