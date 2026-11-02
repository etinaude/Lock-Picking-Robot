#ifndef MAGNET_H
#define MAGNET_H

#include "config.h"
#include <Adafruit_MLX90393.h>
#include <Arduino.h>
#include <Wire.h>

const float MAGNET_R_MM = 2.00f;
const float MAGNET_T_MM = 2.00f;

const float MAGNET_TEMP_PER_C = -0.0012f;
const int TEMP_READ_INTERVAL_MS = 1000;

const float SATURATION_THRESHOLD_MT = 20.0f; // RES_18 on Z: 0.968 uT/LSB
const float OUT_OF_RANGE_THRESHOLD_MT = 2.0f;
const uint8_t MLX90393_STATUS_ERROR_BIT = 0x10;

const float MAX_SPEED_MM_S = 20.0f;
const float JUMP_MARGIN_MM = 0.1f;

class Magnet {
public:
  Adafruit_MLX90393 sensor;
  TwoWire wire;

  float remanenceMT = DEFAULT_CALIBRATION.remanenceMT;
  float zOffsetMM = DEFAULT_CALIBRATION.zOffsetMM;
  float calibrationTempC = DEFAULT_CALIBRATION.calibrationTempC;

  bool ok = false;
  float zmT = 0.0f;
  float tempC = DEFAULT_CALIBRATION.calibrationTempC;
  uint32_t lastTempRead = 0;
  uint32_t lastGoodRead = 0; // 0 until the first reading
  float distanceMM = 0.0f;   // latest reading, what the PID runs on

  Magnet() : wire(1) {}

  bool begin(int sdaPin, int sclPin) {
    wire.begin(sdaPin, sclPin);

    if (!sensor.begin_I2C(MLX90393_DEFAULT_ADDR, &wire))
      return false;

    sensor.setGain(MLX90393_GAIN_1X);
    sensor.setResolution(MLX90393_X, MLX90393_RES_18);
    sensor.setResolution(MLX90393_Y, MLX90393_RES_18);
    sensor.setResolution(MLX90393_Z, MLX90393_RES_18);
    sensor.setOversampling(MLX90393_OSR_2);
    sensor.setFilter(MLX90393_FILTER_3);

    if (!readTemperature(&tempC))
      tempC = calibrationTempC; // no compensation until a good read
    lastTempRead = millis();
    return true;
  }

  void setCalibration(float remanenceMT, float zOffsetMM,
                      float calibrationTempC) {
    this->remanenceMT = remanenceMT;
    this->zOffsetMM = zOffsetMM;
    this->calibrationTempC = calibrationTempC;
    lastGoodRead = 0;
  }

  bool read() {
    if (millis() - lastTempRead >= TEMP_READ_INTERVAL_MS) {
      readTemperature(&tempC);
      lastTempRead = millis();
    }

    float x_uT, y_uT, z_uT;
    ok = sensor.readData(&x_uT, &y_uT, &z_uT);
    if (!ok)
      return false;

    float field = fabs(z_uT) / 1000.0f;
    float fieldScale = 1.0f + MAGNET_TEMP_PER_C * (tempC - calibrationTempC);
    float raw = solveDistance(field / fieldScale) - zOffsetMM;

    uint32_t now = millis();
    float allowedMM =
        MAX_SPEED_MM_S * (now - lastGoodRead) / 1000.0f + JUMP_MARGIN_MM;
    if (lastGoodRead > 0 && fabs(raw - distanceMM) > allowedMM)
      return false;

    lastGoodRead = now;
    zmT = field;
    distanceMM = raw;
    return true;
  }

  // On-axis field of a cylindrical magnet at distance z from its face
  float fieldAt(float z) const {
    float z1 = z + MAGNET_T_MM;
    float r2 = MAGNET_R_MM * MAGNET_R_MM;
    return (remanenceMT / 2.0f) *
           (z1 / sqrt(z1 * z1 + r2) - z / sqrt(z * z + r2));
  }

  float fieldSlopeAt(float z) const {
    float z1 = z + MAGNET_T_MM;
    float r2 = MAGNET_R_MM * MAGNET_R_MM;
    return (remanenceMT / 2.0f) *
           (r2 / pow(z1 * z1 + r2, 1.5f) - r2 / pow(z * z + r2, 1.5f));
  }

  float solveDistance(float bz) const {
    float z = 5.0f; // Initial guess
    for (int i = 0; i < 10; i++) {
      float diff = fieldAt(z) - bz;
      if (fabs(diff) < 0.001f)
        break;

      z -= diff / fieldSlopeAt(z);
      if (z < 0.1f)
        z = 0.1f;
    }
    return z;
  }

  bool readTemperature(float *result) {
    wire.beginTransmission(MLX90393_DEFAULT_ADDR);
    wire.write(0x31);
    if (wire.endTransmission() != 0 ||
        wire.requestFrom(MLX90393_DEFAULT_ADDR, 1) != 1 ||
        (wire.read() & MLX90393_STATUS_ERROR_BIT))
      return false;

    delay(2); // T conversion takes ~0.3 ms with OSR2 = 0

    wire.beginTransmission(MLX90393_DEFAULT_ADDR);
    wire.write(0x41);
    if (wire.endTransmission() != 0 ||
        wire.requestFrom(MLX90393_DEFAULT_ADDR, 3) != 3 ||
        (wire.read() & MLX90393_STATUS_ERROR_BIT))
      return false;

    uint16_t raw = wire.read() << 8;
    raw |= wire.read();
    *result = 25.0f + (raw - 46244.0f) / 45.2f; // datasheet typical TREF
    return true;
  }
};

Magnet magnet;

void setupMagnet() {
  while (!magnet.begin(MAG_SDA_PIN, MAG_SCL_PIN))
    vTaskDelay(pdMS_TO_TICKS(1000));
}

#endif // MAGNET_H
