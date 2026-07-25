#include "Motor.h"

Motor::Motor(uint8_t pwmPin, int8_t directionPin, uint8_t pwmChannel,
             uint32_t pwmFrequencyHz, uint8_t pwmResolutionBits)
    : pwmPin_(pwmPin), directionPin_(directionPin), pwmChannel_(pwmChannel),
      pwmFrequencyHz_(pwmFrequencyHz), pwmResolutionBits_(pwmResolutionBits) {}

void Motor::begin() {
  ledcSetup(pwmChannel_, pwmFrequencyHz_, pwmResolutionBits_);
  ledcAttachPin(pwmPin_, pwmChannel_);

  if (directionPin_ >= 0) {
    pinMode(directionPin_, OUTPUT);
    digitalWrite(directionPin_, LOW);
  }

  stop();
}

void Motor::attachCurrentSensor(CurrentSensor *sensor) {
  currentSensor_ = sensor;
}

void Motor::enable() { enabled_ = true; }

void Motor::disable() {
  stop();
  enabled_ = false;
}

bool Motor::isEnabled() const { return enabled_; }

void Motor::setDirection(Direction direction) {
  direction_ = direction;

  if (directionPin_ >= 0) {
    digitalWrite(directionPin_, direction == Direction::Forward ? LOW : HIGH);
  }
}

Motor::Direction Motor::getDirection() const { return direction_; }

void Motor::setDutyCycle(float dutyCycle) {
  dutyCycle = constrain(dutyCycle, 0.0f, 1.0f);
  dutyCycle_ = dutyCycle;
  applyOutput(output_ < 0.0f ? -dutyCycle_ : dutyCycle_);
}

float Motor::getDutyCycle() const { return dutyCycle_; }

void Motor::setOutput(float output) {
  output = constrain(output, -1.0f, 1.0f);
  output_ = output;
  applyOutput(output_);
}

float Motor::getOutput() const { return output_; }

void Motor::stop(bool brake) {
  output_ = 0.0f;
  dutyCycle_ = 0.0f;

  if (directionPin_ >= 0 && brake) {
    digitalWrite(directionPin_, HIGH);
  }

  ledcWrite(pwmChannel_, 0);
}

void Motor::coast() { stop(false); }

float Motor::readCurrentMilliAmps() {
  if (currentSensor_ == nullptr) {
    return 0.0f;
  }

  return currentSensor_->readCurrentMilliAmps();
}

void Motor::applyOutput(float output) {
  if (!enabled_) {
    ledcWrite(pwmChannel_, 0);
    return;
  }

  output = constrain(output, -1.0f, 1.0f);
  direction_ = output >= 0.0f ? Direction::Forward : Direction::Reverse;

  if (directionPin_ >= 0) {
    digitalWrite(directionPin_, direction_ == Direction::Forward ? LOW : HIGH);
  }

  const float magnitude = fabsf(output);
  const uint32_t maxDuty = (1UL << pwmResolutionBits_) - 1UL;
  const uint32_t duty =
      static_cast<uint32_t>(magnitude * static_cast<float>(maxDuty));
  ledcWrite(pwmChannel_, duty);
}