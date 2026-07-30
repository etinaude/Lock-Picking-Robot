#ifndef MAGNET_H
#define MAGNET_H

#include "state.h"
#include <Arduino.h>
#include <BitBang_I2C.h>
#include <MLX90393.h>

MLX90393 mlx;
MLX90393::txyz data;

void setupMagnet() {
  Wire.begin(MAG_SDA_PIN, MAG_SCL_PIN);

  uint8_t status = mlx.begin(0, 0);
}

float readMagnetData() {
  mlx.readData(data);

  Serial.print(data.x);
  Serial.print(data.y);
  Serial.print(data.z);
  Serial.println(data.t);

  return data.z;
}

float calculateDistance() {
  float z = readMagnetData();
  float distance = (pow(z, 2) * settings.scale) + settings.offset; // TODO
  return distance;
}

#endif