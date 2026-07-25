#include "Arm.h"

Arm::Arm(uint8_t pwmPin, int8_t directionPin, uint8_t pwmChannel,
         uint32_t pwmFrequencyHz, uint8_t pwmResolutionBits, uint8_t mlxA1,
         uint8_t mlxA0)
    : Motor(pwmPin, directionPin, pwmChannel, pwmFrequencyHz,
            pwmResolutionBits),
      mlxA1_(mlxA1), mlxA0_(mlxA0) {}

void Arm::begin() {
  Motor::begin();

  if (hallSensor_.begin(mlxA1_, mlxA0_) != MLX90393::STATUS_OK) {
    calibrated_ = false;
    return;
  }

  updateSensor();
}

bool Arm::calibrateHallZero(uint16_t samples, uint16_t delayMs) {
  if (samples == 0) {
    return false;
  }

  float sumZ = 0.0f;

  for (uint16_t sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    if (!readHallSample(x, y, z)) {
      return false;
    }

    sumZ += z;
    delay(delayMs);
  }

  hallZeroMicrotesla_ = sumZ / static_cast<float>(samples);
  calibrated_ = true;
  updateSensor();
  return true;
}

bool Arm::calibrateHallZeroAtCurrentPosition() { return calibrateHallZero(); }

bool Arm::updateSensor() {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  if (!readHallSample(x, y, z)) {
    return false;
  }

  positionMm_ = (z - hallZeroMicrotesla_) * mmPerMicrotesla_;
  return true;
}

float Arm::getPositionMm() const { return positionMm_; }

float Arm::getTargetPositionMm() const { return targetPositionMm_; }

bool Arm::isCalibrated() const { return calibrated_; }

void Arm::setMmPerMicrotesla(float mmPerMicrotesla) {
  mmPerMicrotesla_ = mmPerMicrotesla;
}

float Arm::getMmPerMicrotesla() const { return mmPerMicrotesla_; }

void Arm::setControlConfig(const ControlConfig &config) {
  controlConfig_ = config;
}

const Arm::ControlConfig &Arm::getControlConfig() const {
  return controlConfig_;
}

bool Arm::moveToPositionMm(float targetMm) {
  return moveToPositionMm(targetMm, controlConfig_);
}

bool Arm::moveToPositionMm(float targetMm, const ControlConfig &config) {
  if (!calibrated_) {
    return false;
  }

  targetPositionMm_ = targetMm;

  float integral = 0.0f;
  float previousError = targetMm - positionMm_;
  uint16_t stableCount = 0;
  float approachTargetMm = positionMm_;
  const float loopDtSec = static_cast<float>(config.loopDelayMs) * 0.001f;

  for (uint16_t iteration = 0; iteration < config.maxIterations; ++iteration) {
    if (!updateSensor()) {
      stop();
      return false;
    }

    const float remainingError = targetMm - positionMm_;
    if (fabsf(remainingError) > config.maxStepMm) {
      approachTargetMm =
          positionMm_ +
          (remainingError > 0.0f ? config.maxStepMm : -config.maxStepMm);
    } else {
      approachTargetMm = targetMm;
    }

    const float error = approachTargetMm - positionMm_;
    if (fabsf(error) <= config.toleranceMm) {
      ++stableCount;
      stop();

      if (stableCount >= 3) {
        return true;
      }

      delay(config.loopDelayMs);
      continue;
    }

    stableCount = 0;

    const float deltaError = error - previousError;
    previousError = error;

    integral = clamp(integral + error * loopDtSec, -config.integralLimit,
                     config.integralLimit);

    float command = config.kp * error + config.ki * integral +
                    config.kd * (deltaError / loopDtSec);
    command = clamp(command, -config.maxOutput, config.maxOutput);

    if (fabsf(command) < config.minOutput) {
      command = command < 0.0f ? -config.minOutput : config.minOutput;
    }

    if (fabsf(error) < 2.0f * config.toleranceMm) {
      command *= 0.5f;
    }

    setOutput(command);
    delay(config.loopDelayMs);
  }

  stop();
  return false;
}

float Arm::readFieldZMicrotesla() const {
  MLX90393::txyz data{};
  if (const_cast<MLX90393 &>(hallSensor_).readData(data) !=
      MLX90393::STATUS_OK) {
    return 0.0f;
  }

  return data.z;
}

bool Arm::readHallSample(float &x, float &y, float &z) {
  MLX90393::txyz data{};
  if (hallSensor_.readData(data) != MLX90393::STATUS_OK) {
    return false;
  }

  x = data.x;
  y = data.y;
  z = data.z;
  return true;
}

float Arm::clamp(float value, float minimum, float maximum) const {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}