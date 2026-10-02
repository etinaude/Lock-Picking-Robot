#ifndef MAGNET_H
#define MAGNET_H

#include "config.h"
#include <Adafruit_MLX90393.h>
#include <Arduino.h>
#include <Wire.h>

const float DEFAULT_REMANENCE_MT = 1114.0f;
const float DEFAULT_Z_OFFSET_MM = 2.85f;
const float DEFAULT_CALIBRATION_TEMP_C = 25.0f;
const float MAGNET_R_MM = 2.0f;
const float MAGNET_T_MM = 2.0f;

const float MAGNET_TEMPCO_PER_C = -0.0012f;
const uint32_t TEMP_READ_INTERVAL_MS = 1000;

const float SATURATION_THRESHOLD_MT = 20.0f; // RES_18 on Z: 0.968 uT/LSB
const float OUT_OF_RANGE_THRESHOLD_MT = 2.0f;
const uint8_t MLX90393_STATUS_ERROR_BIT = 0x10;

class Magnet {
public:
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

  // Values from a probe calibration: effective remanence (mT), the sensor to
  // magnet distance at 0 mm, and the ">Magnet Temp" reading during the fit.
  void setCalibration(float remanenceMT, float zOffsetMM,
                      float calibrationTempC) {
    this->remanenceMT = remanenceMT;
    this->zOffsetMM = zOffsetMM;
    this->calibrationTempC = calibrationTempC;
    sampleCount = 0; // don't average across the old and new calibration
  }

  // One X/Y/Z conversion (blocks ~25 ms), plus a temperature read once a
  // second. Returns false if the sensor didn't respond.
  bool read() {
    if (millis() - lastTempRead >= TEMP_READ_INTERVAL_MS) {
      readTemperature(&tempC); // keeps last value on failure
      lastTempRead = millis();
    }

    float x_uT, y_uT, z_uT;
    ok = sensor.readData(&x_uT, &y_uT, &z_uT);
    if (!ok)
      return false;

    zmT = fabs(z_uT) / 1000.0f;
    float fieldScale = 1.0f + MAGNET_TEMPCO_PER_C * (tempC - calibrationTempC);
    rawDistanceMM = solveDistance(zmT / fieldScale) - zOffsetMM;
    distanceMM = average(rawDistanceMM);
    return true;
  }

  void printErrors() const {
    if (!ok) {
      Serial.println("MAGNET ERROR -> Can not read data.");
    } else if (zmT >= SATURATION_THRESHOLD_MT) {
      Serial.print("MAGNET ERROR -> SATURATED (B_z = ");
      Serial.print(zmT, 2);
      Serial.println(" mT)");
    } else if (zmT < OUT_OF_RANGE_THRESHOLD_MT) {
      Serial.print("MAGNET ERROR -> OUT OF RANGE (B_z = ");
      Serial.print(zmT, 2);
      Serial.println(" mT)");
    }
  }

  float distance() const { return distanceMM; }       // rolling average (mm)
  float rawDistance() const { return rawDistanceMM; } // latest reading (mm)
  float fieldZ() const { return zmT; }
  float temperature() const { return tempC; }

private:
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

  // Newton's method on fieldAt(z) = bz
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

  float average(float sample) {
    if (sampleCount == 0)
      sampleIndex = 0;
    samples[sampleIndex] = sample;
    sampleIndex = (sampleIndex + 1) % DISTANCE_AVERAGE_COUNT;
    if (sampleCount < DISTANCE_AVERAGE_COUNT)
      sampleCount++;

    float total = 0.0f;
    for (uint8_t i = 0; i < sampleCount; i++)
      total += samples[i];
    return total / sampleCount;
  }

  // The Adafruit driver only measures X/Y/Z, so run a temperature-only single
  // measurement with raw commands: SM|T (0x31) then RM|T (0x41).
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

  Adafruit_MLX90393 sensor;
  TwoWire wire;

  float remanenceMT = DEFAULT_REMANENCE_MT;
  float zOffsetMM = DEFAULT_Z_OFFSET_MM;
  float calibrationTempC = DEFAULT_CALIBRATION_TEMP_C;

  bool ok = false;
  float zmT = 0.0f;
  float tempC = DEFAULT_CALIBRATION_TEMP_C;
  uint32_t lastTempRead = 0;
  float rawDistanceMM = 0.0f;
  float distanceMM = 0.0f;

  float samples[DISTANCE_AVERAGE_COUNT];
  uint8_t sampleIndex = 0;
  uint8_t sampleCount = 0;
};

Magnet magnet;

#endif // MAGNET_H
