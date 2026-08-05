#ifndef MOTOR_H
#define MOTOR_H

#include "magnet.h"
#include "state.h"
#include <PID_v1.h>

PID motorPID(&currentDistance, &motorPWM, &targetDistance, settings.Kp,
             settings.Ki, settings.Kd, DIRECT);

void setupMotor() {
  motorPID.SetOutputLimits(-255, 255);
  motorPID.SetMode(AUTOMATIC);
}

void setMotorSpeed(int speed) {
  if (speed < 0)
    speed = 0;
  else if (speed > 255)
    speed = 255;

  analogWrite(PWM_PIN, speed);
}

void handlePID() {
  motorPID.Compute();
  setMotorSpeed(motorPWM);
}

#endif