
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

struct __attribute__((packed)) ArmStatePayload {
  struct {
    float x;
    float y;
    float z;
    float t;
    float pos;
  } magnet;
  float motorPWM;
  float motorCurrent;
};

String payloadToJson(const ArmStatePayload &payload) {
  StaticJsonDocument<200> doc;
  doc["magnet"]["x"] = payload.magnet.x;
  doc["magnet"]["y"] = payload.magnet.y;
  doc["magnet"]["z"] = payload.magnet.z;
  doc["magnet"]["t"] = payload.magnet.t;
  doc["magnet"]["pos"] = payload.magnet.pos;
  doc["motorPWM"] = payload.motorPWM;
  doc["motorCurrent"] = payload.motorCurrent;

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

void payloadFromJson(ArmStatePayload &payload, const String &jsonString) {
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  payload.magnet.x = doc["magnet"]["x"] | 0.0;
  payload.magnet.y = doc["magnet"]["y"] | 0.0;
  payload.magnet.z = doc["magnet"]["z"] | 0.0;
  payload.magnet.t = doc["magnet"]["t"] | 0.0;
  payload.magnet.pos = doc["magnet"]["pos"] | 0.0;
  payload.motorPWM = doc["motorPWM"] | 0.0;
  payload.motorCurrent = doc["motorCurrent"] | 0.0;
}

#endif