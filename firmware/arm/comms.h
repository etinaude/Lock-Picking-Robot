#ifndef COMMS_H
#define COMMS_H

#include "motor.h"
#include "state.h"

void setupComms() {
  Serial.begin(115200);
  lastSentTime = millis();
}

// void sendSerial() {
//   if (millis() - lastSentTime >= 1000) { // Send data every 1 second
//     String jsonString;
//     // Serial.println(payloadToJson(state));
//     lastSentTime = millis();
//   }
// }

void receiveSerial() {
  if (Serial.available() > 0) {
    String jsonString = Serial.readStringUntil('\n');
    // receive as a number
    targetDistance = jsonString.toFloat();
  }
}

#endif