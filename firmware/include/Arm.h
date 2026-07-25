#pragma once

#include "Motor.h"
#include <MLX90393.h>

class Arm : public Motor {
public:
  struct ControlConfig {
    float kp = 0.9f;
    float ki = 0.0f;
    float kd = 0.06f;
    float toleranceMm = 0.05f;
    float maxOutput = 0.8f;
    float minOutput = 0.08f;
    float integralLimit = 0.25f;
    float maxStepMm = 0.5f;
    uint16_t maxIterations = 400;
    uint16_t loopDelayMs = 5;
  };

  Arm(uint8_t pwmPin, int8_t directionPin = -1, uint8_t pwmChannel = 0,
      uint32_t pwmFrequencyHz = 20000, uint8_t pwmResolutionBits = 10,
      uint8_t mlxA1 = 0, uint8_t mlxA0 = 0);

  void begin();

  bool calibrateHallZero(uint16_t samples = 32, uint16_t delayMs = 5);
  bool calibrateHallZeroAtCurrentPosition();

  bool updateSensor();
  float getPositionMm() const;
  float getTargetPositionMm() const;
  bool isCalibrated() const;

  void setMmPerMicrotesla(float mmPerMicrotesla);
  float getMmPerMicrotesla() const;

  void setControlConfig(const ControlConfig &config);
  const ControlConfig &getControlConfig() const;

  bool moveToPositionMm(float targetMm);
  bool moveToPositionMm(float targetMm, const ControlConfig &config);

  float readFieldZMicrotesla() const;

private:
  bool readHallSample(float &x, float &y, float &z);
  float clamp(float value, float minimum, float maximum) const;

  MLX90393 hallSensor_;
  uint8_t mlxA1_;
  uint8_t mlxA0_;

  ControlConfig controlConfig_;
  bool calibrated_ = false;
  float hallZeroMicrotesla_ = 0.0f;
  float mmPerMicrotesla_ = 1.0f;
  float positionMm_ = 0.0f;
  float targetPositionMm_ = 0.0f;
};