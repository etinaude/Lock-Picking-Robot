#ifndef MAGNET_H
#define MAGNET_H

#include "state.h"
#include <Arduino.h>
#include <BitBang_I2C.h>
#include <MLX90393.h>

MLX90393 mlx;
TwoWire magnetWire(1);

void setupMagnet() {
  magnetWire.begin(MAG_SDA_PIN, MAG_SCL_PIN);

  uint8_t status = mlx.begin(0, 0);
}

float readMagnetData() {
  mlx.readData(magnetData);

  Serial.print(magnetData.x);
  Serial.print(magnetData.y);
  Serial.print(magnetData.z);
  Serial.println(magnetData.t);

  return magnetData.z;
}

float calculateDistance() {
  float z = readMagnetData();
  float distance = (pow(z, 2) * settings.scale) + settings.offset; // TODO
  return distance;
}

#endif