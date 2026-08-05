#ifndef COMMS_H
#define COMMS_H

#include "motor.h"
#include "state.h"
#include <Wire.h>
#include <esp_mac.h>

TwoWire commWire(0);

void receiveEvent(int incomingData) {
  // Read the incoming bytes into a buffer
  String jsonString;
  while (commWire.available()) {
    char c = commWire.read();
    jsonString += c;
  }

  if (incomingData >= sizeof(float)) {
    float incomingGoal = 0.0;
    // Read the incoming bytes directly into the float's memory address
    commWire.readBytes((char *)&incomingGoal, sizeof(float));

    // Safely update setpoint
    targetDistance = incomingGoal;
  }
}

void sendData() {
  setPayload();
  commWire.write(payloadToJson(state).c_str());
}

void setupComms() {
  // Initialize Serial communication
  Serial.begin(9600);
  lastSentTime = millis();

  // get mac
  uint8_t mac[6];
  if (esp_base_mac_addr_get(mac) == ESP_OK) {
    Serial.printf("Base MAC: " MACSTR "\n", MAC2STR(mac));
  }

  String currentMac = String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" +
                      String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" +
                      String(mac[4], HEX) + ":" + String(mac[5], HEX);

  // get the settings for this mac address
  for (int i = 0; i < 6; i++) {
    if (lookupTable[i]->macAddress == currentMac) {
      settings = *lookupTable[i];
      Serial.printf("Found settings for MAC: %s\n", currentMac.c_str());
      Serial.printf(
          "Kp: %f, Ki: %f, Kd: %f, scal: %f, offset: %f, i2c: 0x%02X\n",
          settings.Kp, settings.Ki, settings.Kd, settings.scale,
          settings.offset, settings.i2cAddress);
      break;
    }
  }

  // Initialize I2C communication
  commWire.begin(settings.i2cAddress, COMMS_SDA_PIN, COMMS_SCL_PIN);
  commWire.onReceive(receiveEvent);
  commWire.onRequest(sendData);
}

void sendSerial() {
  if (millis() - lastSentTime >= 1000) { // Send data every 1 second
    String jsonString;
    Serial.println(payloadToJson(state));
    lastSentTime = millis();
  }
}

void receiveSerial() {
  if (Serial.available() > 0) {
    String jsonString = Serial.readStringUntil('\n');
    payloadFromJson(state, jsonString);
  }
}

#endif