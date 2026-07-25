#pragma once

#include <Arduino.h>

class CurrentSensor {
public:
  virtual ~CurrentSensor() = default;
  virtual float readCurrentMilliAmps() = 0;
};

class Motor {
public:
  enum class Direction {
    Forward,
    Reverse,
  };

  Motor(uint8_t pwmPin, int8_t directionPin = -1, uint8_t pwmChannel = 0,
        uint32_t pwmFrequencyHz = 20000, uint8_t pwmResolutionBits = 10);
  virtual ~Motor() = default;

  void begin();
  void attachCurrentSensor(CurrentSensor *sensor);

  void enable();
  void disable();
  bool isEnabled() const;

  void setDirection(Direction direction);
  Direction getDirection() const;

  void setDutyCycle(float dutyCycle);
  float getDutyCycle() const;

  void setOutput(float output);
  float getOutput() const;

  void stop(bool brake = false);
  void coast();

  float readCurrentMilliAmps();

protected:
  void applyOutput(float output);

  uint8_t pwmPin_;
  int8_t directionPin_;
  uint8_t pwmChannel_;
  uint32_t pwmFrequencyHz_;
  uint8_t pwmResolutionBits_;

  bool enabled_ = false;
  Direction direction_ = Direction::Forward;
  float dutyCycle_ = 0.0f;
  float output_ = 0.0f;
  CurrentSensor *currentSensor_ = nullptr;
};