#ifndef MAGNET_H
#define MAGNET_H

#include "state.h"
#include <Adafruit_MLX90393.h>
#include <Arduino.h>
#include <MLX90393.h>
#include <Wire.h>

const float MAGNET_BR_MT = 1450.0;           // N52 remanence (mT)
const float MAGNET_D_MM = 4.0;               // Diameter (mm)
const float MAGNET_R_MM = MAGNET_D_MM / 2.0; // Radius (mm)
const float MAGNET_T_MM = 2.0;               // Thickness (mm)
const float Z_OFFSET_MM = 3.5;               // 0 point (mm)
const float SATURATION_THRESHOLD_MT = 6.5;

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

void setupMagnet() {
  magnetWire.begin(MAG_SDA_PIN, MAG_SCL_PIN);

  // sensor.begin_I2C(magnetWire)

  if (!sensor.begin_I2C()) {
    Serial.println("Error: MLX90393 magnetometer not detected!");
    while (1)
      delay(100);
  }

  sensor.setGain(MLX90393_GAIN_1X);
  sensor.setResolution(MLX90393_X, MLX90393_RES_16);
  sensor.setResolution(MLX90393_Y, MLX90393_RES_16);
  sensor.setResolution(MLX90393_Z, MLX90393_RES_16);
  sensor.setOversampling(MLX90393_OSR_2);
  sensor.setFilter(MLX90393_FILTER_4);
}

void printPacket() {
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
    Serial.print("B_z: ");
    Serial.print(state.magnet.zmT, 2);
    Serial.print(" mT | Total Dist: ");
    Serial.print(state.magnet.totalDist, 2);
    Serial.print(" mm | Motion Dist: ");
    Serial.print(state.magnet.motionDist, 2);
    Serial.println(" mm");
  }
}

void readMagnet() {
  float x_uT, y_uT, z_uT;
  if (sensor.readData(&x_uT, &y_uT, &z_uT)) {
    state.magnet.zmT = fabs(z_uT) / 1000.0f;
    state.magnet.xmT = fabs(x_uT) / 1000.0f;
    state.magnet.ymT = fabs(y_uT) / 1000.0f;
    state.magnet.totalDist = solveTotalDistance(state.magnet.zmT);
    state.magnet.motionDist = state.magnet.totalDist - Z_OFFSET_MM;

  } else {
    state.magnet.status = 1;
    Serial.println("Failed to read sensor.");
  }
}

#endif // MAGNET_H