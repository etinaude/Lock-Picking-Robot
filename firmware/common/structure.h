
#ifndef STRUCTURE_H
#define STRUCTURE_H

#include <ArduinoJson.h>

class ArmSettings {
public:
  String macAddress;
  float Kp;
  float Ki;
  float Kd;
  double scale;
  double offset;
  uint8_t i2cAddress;

  ArmSettings()
      : Kp(0), Ki(0), Kd(0), macAddress(""), scale(0), offset(0),
        i2cAddress(0) {}

  ArmSettings(const String &macAddress, float Kp, float Ki, float Kd,
              double scale, double offset, uint8_t i2cAddress)
      : Kp(Kp), Ki(Ki), Kd(Kd), macAddress(macAddress), scale(scale),
        offset(offset), i2cAddress(i2cAddress) {}

  String toJson() {
    StaticJsonDocument<200> doc;
    doc["Kp"] = Kp;
    doc["Ki"] = Ki;
    doc["Kd"] = Kd;
    doc["macAddress"] = macAddress;
    doc["scale"] = scale;
    doc["offset"] = offset;
    doc["i2cAddress"] = i2cAddress;
  }

  void fromJson(const String &jsonString) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    Kp = doc["Kp"] | 0.0;
    Ki = doc["Ki"] | 0.0;
    Kd = doc["Kd"] | 0.0;
    macAddress = doc["macAddress"] | "";
    scale = doc["scale"] | 0.0;
    offset = doc["offset"] | 0.0;
    i2cAddress = doc["i2cAddress"] | 0;
  }
};

struct ArmData {
  struct {
    uint8_t status;
    float totalDist;
    float motionDist;
    float xmT;
    float ymT;
    float zmT;
    float t;
    int state;
  } magnet;
  float motorPWM;
  float motorCurrent;
};

#endif