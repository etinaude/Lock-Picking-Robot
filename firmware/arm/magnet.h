#ifndef MAGNET_H
#define MAGNET_H

#include "state.h"
#include <Adafruit_MLX90393.h>
#include <Arduino.h>
#include <MLX90393.h>
#include <Wire.h>

// MAGNET_BR_MT and Z_OFFSET_MM are fitted to dial-probe measurements over
// 3-10 mm (rms residual 0.044 mm), not datasheet values. The effective
// remanence is ~23% below N52's 1450 mT because it also absorbs sensor gain
// error, the depth of the Hall plate inside the package, and any off-axis
// alignment.
// Re-fit both if the magnet, sensor, or mounting changes.
const float MAGNET_BR_MT = 1114.0;           // Effective remanence (mT)
const float MAGNET_D_MM = 4.0;               // Diameter (mm)
const float MAGNET_R_MM = MAGNET_D_MM / 2.0; // Radius (mm)
const float MAGNET_T_MM = 2.0;               // Thickness (mm)
const float Z_OFFSET_MM = 2.85;              // 0 point (mm)
const float SATURATION_THRESHOLD_MT = 20.0; // RES_18 on Z: 0.968 uT/LSB

// NdFeB loses ~0.12% of its field per degree C (reversible), which is ~0.05 mm
// at 10 mm for a 10 C swing. The sensor die temperature stands in for the
// magnet's, and CALIBRATION_TEMP_C must be the ">Magnet Temp" reading from
// when MAGNET_BR_MT and Z_OFFSET_MM were fitted.
const float MAGNET_TEMPCO_PER_C = -0.0012f;
const float CALIBRATION_TEMP_C = 25.0f;
const uint32_t TEMP_READ_INTERVAL_MS = 1000;
const uint8_t MLX90393_STATUS_ERROR_BIT = 0x10;
uint32_t lastTempRead = 0;

float distanceSamples[DISTANCE_AVERAGE_COUNT];
uint8_t distanceSampleIndex = 0;
uint8_t distanceSampleCount = 0;

Adafruit_MLX90393 sensor = Adafruit_MLX90393();
TwoWire magnetWire(1);

float calculateBz(float z) {
  float z1 = z + MAGNET_T_MM;
  float r2 = MAGNET_R_MM * MAGNET_R_MM;
  float term1 = z1 / sqrt(z1 * z1 + r2);
  float term2 = z / sqrt(z * z + r2);
  return (MAGNET_BR_MT / 2.0f) * (term1 - term2);
}

float calculatedBdz(float z) {
  float z1 = z + MAGNET_T_MM;
  float r2 = MAGNET_R_MM * MAGNET_R_MM;
  float dt1 = r2 / pow(z1 * z1 + r2, 1.5f);
  float dt2 = r2 / pow(z * z + r2, 1.5f);
  return (MAGNET_BR_MT / 2.0f) * (dt1 - dt2);
}

float solveTotalDistance(float target_B_mT) {
  float z = 5.0f; // Initial guess

  for (int i = 0; i < 10; i++) {
    float b_calc = calculateBz(z);
    float db_calc = calculatedBdz(z);
    float diff = b_calc - target_B_mT;

    if (fabs(diff) < 0.001f)
      break;

    z = z - diff / db_calc;
    if (z < 0.1f)
      z = 0.1f;
  }
  return z;
}

float averageDistance(float sample) {
  distanceSamples[distanceSampleIndex] = sample;
  distanceSampleIndex = (distanceSampleIndex + 1) % DISTANCE_AVERAGE_COUNT;
  if (distanceSampleCount < DISTANCE_AVERAGE_COUNT)
    distanceSampleCount++;

  float total = 0.0f;
  for (uint8_t i = 0; i < distanceSampleCount; i++)
    total += distanceSamples[i];
  return total / distanceSampleCount;
}

// The Adafruit driver only measures X/Y/Z, so run a temperature-only single
// measurement with raw commands: SM|T (0x31) then RM|T (0x41).
bool readMagnetTemperature(float *tempC) {
  magnetWire.beginTransmission(MLX90393_DEFAULT_ADDR);
  magnetWire.write(0x31);
  if (magnetWire.endTransmission() != 0 ||
      magnetWire.requestFrom(MLX90393_DEFAULT_ADDR, 1) != 1 ||
      (magnetWire.read() & MLX90393_STATUS_ERROR_BIT))
    return false;

  delay(2); // T conversion takes ~0.3 ms with OSR2 = 0

  magnetWire.beginTransmission(MLX90393_DEFAULT_ADDR);
  magnetWire.write(0x41);
  if (magnetWire.endTransmission() != 0 ||
      magnetWire.requestFrom(MLX90393_DEFAULT_ADDR, 3) != 3 ||
      (magnetWire.read() & MLX90393_STATUS_ERROR_BIT))
    return false;

  uint16_t raw = magnetWire.read() << 8;
  raw |= magnetWire.read();
  *tempC = 25.0f + (raw - 46244.0f) / 45.2f; // datasheet typical TREF
  return true;
}

bool setupMagnet() {
  magnetWire.begin(MAG_SDA_PIN, MAG_SCL_PIN);

  if (!sensor.begin_I2C(MLX90393_DEFAULT_ADDR, &magnetWire))
    return false;

  sensor.setGain(MLX90393_GAIN_1X);
  sensor.setResolution(MLX90393_X, MLX90393_RES_18);
  sensor.setResolution(MLX90393_Y, MLX90393_RES_18);
  sensor.setResolution(MLX90393_Z, MLX90393_RES_18);
  sensor.setOversampling(MLX90393_OSR_2);
  sensor.setFilter(MLX90393_FILTER_4);

  if (!readMagnetTemperature(&state.magnet.t))
    state.magnet.t = CALIBRATION_TEMP_C; // no compensation until a good read
  lastTempRead = millis();
  return true;
}

void printMagnet() {
  if (state.magnet.status != 0) {
    Serial.println("MAGNET ERROR -> Can not read data.");
  } else if (state.magnet.zmT >= SATURATION_THRESHOLD_MT) {
    Serial.print("MAGNET ERROR -> SATURATED (B_z = ");
    Serial.print(state.magnet.zmT, 2);
    Serial.println(" mT)");
  } else if (state.magnet.zmT < 2.0f) {
    Serial.print(state.magnet.zmT, 2);

    Serial.println("MAGNET ERROR -> OUT OF RANGE (B_z = ");
    Serial.print(state.magnet.zmT, 2);
    Serial.println(" mT)");
  } else {
    // Serial.print(">B_z: ");
    // Serial.println(state.magnet.zmT, 2);
    // Serial.print(">Motion Dist: ");
    // Serial.println(state.magnet.motionDist, 2);
  }
}

void readMagnet() {
  if (millis() - lastTempRead >= TEMP_READ_INTERVAL_MS) {
    readMagnetTemperature(&state.magnet.t); // keeps last value on failure
    lastTempRead = millis();
  }

  float x_uT, y_uT, z_uT;
  if (sensor.readData(&x_uT, &y_uT, &z_uT)) {
    state.magnet.zmT = fabs(z_uT) / 1000.0f;
    state.magnet.xmT = fabs(x_uT) / 1000.0f;
    state.magnet.ymT = fabs(y_uT) / 1000.0f;
    float fieldScale =
        1.0f + MAGNET_TEMPCO_PER_C * (state.magnet.t - CALIBRATION_TEMP_C);
    state.magnet.totalDist = solveTotalDistance(state.magnet.zmT / fieldScale);
    state.magnet.motionDist = state.magnet.totalDist - Z_OFFSET_MM;
    currentDistance = averageDistance(state.magnet.motionDist);
    state.magnet.status = 0;
  } else {
    state.magnet.status = 1;
    Serial.println("Failed to read sensor.");
  }
}

#endif // MAGNET_H