#include <Arduino.h>

#include "Arm.h"

namespace {
constexpr uint8_t kMotorPwmPin = 4;
constexpr int8_t kMotorDirectionPin = 5;
constexpr uint8_t kMotorPwmChannel = 0;
constexpr uint8_t kHallAddressA1 = 0;
constexpr uint8_t kHallAddressA0 = 0;

Arm arm(kMotorPwmPin, kMotorDirectionPin, kMotorPwmChannel, 20000, 10,
        kHallAddressA1, kHallAddressA0);
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  arm.begin();
  arm.setMmPerMicrotesla(0.01f);

  if (!arm.calibrateHallZeroAtCurrentPosition()) {
    Serial.println("Arm hall calibration failed");
    return;
  }

  Arm::ControlConfig controlConfig;
  controlConfig.kp = 1.2f;
  controlConfig.ki = 0.0f;
  controlConfig.kd = 0.08f;
  controlConfig.toleranceMm = 0.05f;
  controlConfig.maxOutput = 0.7f;
  controlConfig.minOutput = 0.06f;
  arm.setControlConfig(controlConfig);

  arm.enable();
  const bool moved = arm.moveToPositionMm(5.0f);
  if (!moved) {
    Serial.println("Arm move failed");
  }
}

void loop() { delay(1000); }