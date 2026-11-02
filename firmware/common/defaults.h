// clang-format off

#pragma once

#include <Arduino.h>
#include <esp_mac.h>

#define DISTANCE_DEADBAND 0.04f

struct ArmCalibration {
  const char *mac;
  int id;                 // (generally Lock Pin)
  float minPWM;
  double kp, ki, kd;
  float remanenceMT;
  float zOffsetMM;
  float calibrationTempC;
};


const ArmCalibration ARM_CALIBRATIONS[] = {
  //  MAC                ID  Min PWM  Kp       Ki       Kd        mT        Z offset  Cal temp C
  {"",                  0,  30.0f,   250.0,   0.0,     8.0,     921.7f,       2.324f,      31.04f},
  {"98:C3:77:63:53:B8", 1,  8.0f,    200.0,   0.0,     8.0,     921.7f,       2.324f,      31.04f}
};

const ArmCalibration DEFAULT_CALIBRATION = ARM_CALIBRATIONS[0];

String macAddress() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return text;
}

const ArmCalibration &findCalibration(const String &mac) {
  for (const ArmCalibration &calibration : ARM_CALIBRATIONS)
    if (mac.equalsIgnoreCase(calibration.mac))
      return calibration;
  return DEFAULT_CALIBRATION;
}

void printCalibration(const ArmCalibration &calibration) {
  String mac = macAddress();
  const ArmCalibration &row = findCalibration(mac);
  if (&row == &DEFAULT_CALIBRATION)
    Serial.printf("MAC: %s (not in defaults.h)\n", mac.c_str());
  else
    Serial.printf("MAC: %s (arm %d)\n", mac.c_str(), row.id);
  Serial.printf("Calibration row: {\"%s\", %d, %.1ff, %.4f, %.4f, %.4f, "
                "%.1ff, %.3ff, %.2ff},\n",
                mac.c_str(), calibration.id, calibration.minPWM, calibration.kp,
                calibration.ki, calibration.kd, calibration.remanenceMT,
                calibration.zOffsetMM, calibration.calibrationTempC);
}
