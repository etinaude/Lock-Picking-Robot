#ifndef MOTOR_H
#define MOTOR_H

// DRV8876 MOTOR

#include "config.h"
#include "magnet.h"
#include "state.h"

#include <PID_v1.h>
float filteredCurrent = 0.0f;

PID motorPID(&currentDistance, &motorPWM, &targetDistance, settings.Kp,
             settings.Ki, settings.Kd, DIRECT);

void setupMotor() {
  analogReadResolution(12); // Sets ADC resolution to 0 - 4095
  analogSetPinAttenuation(PIN_CS, ADC_11db);
  pinMode(IN1_EN, OUTPUT);
  pinMode(IN2_PH, OUTPUT);
  pinMode(MODE_PIN, OUTPUT);
  digitalWrite(MODE_PIN, HIGH); // Set to PH/EN mode

  ledcSetup(IN1_EN, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(IN1_EN, IN1_EN);

  motorPID.SetOutputLimits(-255, 255);
  motorPID.SetMode(AUTOMATIC);
}

static float readMotorCurrent() {
  uint32_t adcTotal = 0;
  for (uint8_t sample = 0; sample < CURRENT_SAMPLE_COUNT; sample++) {
    adcTotal += analogRead(PIN_CS);
    delayMicroseconds(50);
  }

  float adcRaw = adcTotal / (float)CURRENT_SAMPLE_COUNT;
  float voltage = (adcRaw / 4095.0f) * 3.3f; // Measured voltage on CS pin

  // Calculate motor current (scale assumes standard 2.5 V/A ratio)
  float currentAmps = voltage / 2.5;
  float currentMilliamps = currentAmps * 1000.0f;
  filteredCurrent +=
      CURRENT_FILTER_ALPHA * (currentMilliamps - filteredCurrent);

  return filteredCurrent;
}

void stopMotor() { ledcWrite(IN1_EN, 0); }

void setMotorPID(double pidValue) {
  if (pidValue == 0.0) {
    stopMotor();
    return;
  }

  bool driveBackward = pidValue > 0.0;
  double pwmValue = fabs(pidValue);
  if (pwmValue > 255.0)
    pwmValue = 255.0;

  digitalWrite(IN2_PH, driveBackward ? HIGH : LOW);
  ledcWrite(IN1_EN, static_cast<uint8_t>(pwmValue));
}

void handlePID() {
  motorPID.Compute();
  setMotorPID(motorPWM);
}

#endif