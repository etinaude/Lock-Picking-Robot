#include "config.h"
#include <MLX90393.h>

#ifndef STATE_H
#define STATE_H

int lastSentTime = 0;
ArmSettings settings;
ArmStatePayload state;

MLX90393::txyz magnetData;

double motorPWM = 0.0;
double motorCurrent = 0.0;
double currentDistance = 0.0;
double targetDistance = 0.0;

void setPayload() {
  state.magnet.x = magnetData.x;
  state.magnet.y = magnetData.y;
  state.magnet.z = magnetData.z;
  state.magnet.t = magnetData.t;
  state.magnet.pos = currentDistance;
  state.motorPWM = motorPWM;
  state.motorCurrent = motorCurrent;
}

#endif // STATE_H