#pragma once

#include "calibration.h"
#include <Arduino.h>
#include <ArduinoJson.h>

void printMenu() {
  Serial.println();
  Serial.println("Calibration commands:");
  Serial.println("  P   PID (not implemented yet)");
  Serial.println("  C   motor current (not implemented yet)");
  Serial.println("  M   magnetometer sweep, 3-10 mm in 0.5 mm steps");
  Serial.println("  R   repeatability, 3 <-> 10 mm x10 (R5 = 5 times)");
  Serial.println("  V   set calibration values, as JSON");
  Serial.println("  #   go to that distance in mm, 3-10, e.g. 7.5");
}

void dropInputSentDuringRun() {
  while (Serial.available())
    Serial.read();
}

bool setValues(const char *json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.printf("Parameters not set: %s\n", error.c_str());
    return false;
  }

  ArmCalibration calibration = activeCalibration;
  calibration.minPWM = doc["minPWM"];
  calibration.kp = doc["kp"];
  calibration.ki = doc["ki"];
  calibration.kd = doc["kd"];
  calibration.remanenceMT = doc["remanenceMT"];
  calibration.zOffsetMM = doc["zOffsetMM"];
  calibration.calibrationTempC = doc["calibrationTempC"];
  setCalibration(calibration);
  Serial.println("Parameters set, kept until reset");
  return true;
}

void runMenuCommand(const String &command) {
  if (command.isEmpty())
    return;

  if (isDigit(command[0]) || command[0] == '.') {
    goTo(command.toFloat());
    return;
  }

  switch (toupper(command[0])) {
  case 'P':
    Serial.println("PID calibration is not implemented yet.");
    break;
  case 'C':
    Serial.println("Motor current calibration is not implemented yet.");
    break;
  case 'M':
    runMagnetometer();
    break;
  case 'R':
    runRepeatability(command.length() > 1 ? command.substring(1).toInt()
                                          : DEFAULT_REPEATS);
    break;
  case 'V': // the UI takes the Calibration row line as the end of the reply
    if (command.length() == 1 || setValues(command.c_str() + 1))
      printCalibration(activeCalibration);
    break;
  }
  dropInputSentDuringRun();
}

void handleSerial() {
  if (!Serial.available())
    return;

  String command = Serial.readStringUntil('\n');
  command.trim();
  runMenuCommand(command);
}

void setupComms() { Serial.begin(115200); }
