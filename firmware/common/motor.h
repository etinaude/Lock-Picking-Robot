#ifndef MOTOR_H
#define MOTOR_H

// DRV8876 MOTOR
// PH high is towards Motor, PH low is away from Motor (PH/EN mode)

#include "config.h"
#include <Arduino.h>
#include <PID_v1.h>

const double DEFAULT_KP = 3.0;
const double DEFAULT_KI = 0.1;
const double DEFAULT_KD = 1.0;
const double DEFAULT_TARGET_MM = 6.0;
// PID_v1 scales Ki and Kd by this, but Compute runs once per magnet reading
// (~25 ms). It must stay below the reading period or readings get skipped.
const int PID_SAMPLE_TIME_MS = 20;

class Motor {
public:
  void begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_CS, ADC_11db);
    pinMode(IN1_EN, OUTPUT);
    pinMode(IN2_PH, OUTPUT);
    pinMode(MODE_PIN, OUTPUT);
    digitalWrite(MODE_PIN, LOW);
    ledcSetup(0, 10000, 8);
    ledcAttachPin(IN1_EN, 0);

    pid.SetOutputLimits(-255, 255);
    pid.SetSampleTime(PID_SAMPLE_TIME_MS);
    pid.SetMode(AUTOMATIC);
  }

  void setPIDCalibration(double kp, double ki, double kd) {
    pid.SetTunings(kp, ki, kd);
  }

  void setTarget(double targetMM) {
    pid.SetMode(MANUAL);
    setpoint = constrain(targetMM, TRAVEL_MIN_MM, TRAVEL_MAX_MM);
    output = 0.0;
    pid.SetMode(AUTOMATIC);
  }

  // One control step towards the target from the measured distance (mm)
  void update(float distanceMM) {
    input = distanceMM;
    if (fabs(input - setpoint) <= DISTANCE_DEADBAND) {
      pid.SetMode(MANUAL);
      output = 0.0;
      pid.SetMode(AUTOMATIC);
      stop();
      return;
    }

    pid.Compute();

    // Near the target the derivative term pushes back against the approach.
    // Brake (EN low) for that rather than kicking into reverse at MIN_PWM.
    if ((output > 0.0) != (setpoint > input)) {
      stop();
      return;
    }
    drive(output);
  }

  void stop() {
    ledcWrite(0, 0);
    pwmValue = 0.0;
  }

  float readCurrent() {
    uint32_t adcTotal = 0;
    for (uint8_t sample = 0; sample < CURRENT_SAMPLE_COUNT; sample++) {
      adcTotal += analogRead(PIN_CS);
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

  void drive(double pidValue) {
    if (pidValue == 0.0) {
      stop();
      return;
    }

    bool driveBackward = pidValue > 0.0;
    double pwm = fabs(pidValue);
    pwm += MOTOR_MIN_PWM; // overcome static friction for small errors
    setPWM(driveBackward ? pwm : -pwm);
  }

  // Signed PWM straight to the driver, no minimum added; positive drives
  // towards larger distances
  void setPWM(double pwm) {
    if (pwm == 0.0) {
      stop();
      return;
    }
    pwmValue = fmin(fabs(pwm), MOTOR_MAX_PWM);
    digitalWrite(IN2_PH, pwm > 0.0 ? LOW : HIGH);
    ledcWrite(0, static_cast<uint8_t>(pwmValue));
  }

  double input = 0.0;
  double output = 0.0;
  double setpoint = DEFAULT_TARGET_MM;
  double pwmValue = 0.0;
  float filteredCurrent = 0.0f;

  PID pid{&input,     &output,    &setpoint, DEFAULT_KP,
          DEFAULT_KI, DEFAULT_KD, DIRECT};
};

Motor motor;

#endif
