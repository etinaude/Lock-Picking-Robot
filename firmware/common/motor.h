#pragma once

#include "config.h"
#include "defaults.h"
#include <Arduino.h>
#include <PID_v1.h>

const double DEFAULT_TARGET_MM = 6.0;
const int PID_SAMPLE_TIME_MS = 20;

class Motor {
public:
  double input = 0.0;
  double output = 0.0;
  double target = DEFAULT_TARGET_MM;
  double pwmValue = 0.0;
  float filteredCurrent = 0.0f;
  float minPWM = DEFAULT_CALIBRATION.minPWM;

  PID pid{&input,
          &output,
          &target,
          DEFAULT_CALIBRATION.kp,
          DEFAULT_CALIBRATION.ki,
          DEFAULT_CALIBRATION.kd,
          DIRECT};

  void begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_IPROPI, ADC_11db);
    pinMode(M_PWM_PIN, OUTPUT);
    pinMode(M_DIR_PIN, OUTPUT);
    pinMode(PIN_nFAULT, INPUT);
    pinMode(PIN_nSLEEP, OUTPUT);
    digitalWrite(PIN_nSLEEP, HIGH);
    delay(20);
    ledcSetup(0, 10000, 8);
    ledcAttachPin(M_PWM_PIN, 0);

    pid.SetOutputLimits(-255, 255);
    pid.SetSampleTime(PID_SAMPLE_TIME_MS);
    pid.SetMode(AUTOMATIC);
  }

  void setCalibration(double kp, double ki, double kd, float minPWM) {
    pid.SetTunings(kp, ki, kd);
    this->minPWM = minPWM;
  }

  void setTarget(double targetMM) {
    pid.SetMode(MANUAL);
    target = constrain(targetMM, TRAVEL_MIN_MM, TRAVEL_MAX_MM);
    output = 0.0;
    pid.SetMode(AUTOMATIC);
  }

  void update(float distanceMM) {
    input = distanceMM;

    if (fabs(input - target) <= DISTANCE_DEADBAND)
      return stop();

    pid.Compute();

    // Near the target the derivative term pushes back against the approach.
    // Brake (EN low) for that rather than kicking into reverse at MIN_PWM.
    if ((output > 0.0) != (target > input))
      return stop();

    setPWM(output);
  }

  void stop() {
    ledcWrite(0, 0);
    pwmValue = 0.0;
  }

  float readCurrent() {
    uint32_t adcTotal = 0;
    for (uint8_t sample = 0; sample < CURRENT_SAMPLE_COUNT; sample++) {
      adcTotal += analogRead(PIN_IPROPI);
      delayMicroseconds(50);
    }

    float adcRaw = adcTotal / (float)CURRENT_SAMPLE_COUNT;
    float voltage = (adcRaw / 4095.0f) * 3.3f; // Measured voltage on CS pin

    // IPROPI volts per amp depends on its resistor
    float currentMilliamps = voltage / CURRENT_SENSE_V_PER_A * 1000.0f;
    filteredCurrent +=
        CURRENT_FILTER_ALPHA * (currentMilliamps - filteredCurrent);
    return filteredCurrent;
  }

  void setPWM(double pwm) {
    if (pwm == 0.0)
      return stop();

    bool direction = pwm > 0.0;
    double pwmValue = fmin(fabs(pwm) + minPWM, MOTOR_MAX_PWM);

    digitalWrite(M_DIR_PIN, direction);
    ledcWrite(0, pwmValue);
  }
};

Motor motor;
