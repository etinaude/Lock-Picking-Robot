#include "config.h"
#include <MLX90393.h>

#ifndef STATE_H
#define STATE_H

int lastSentTime = 0;
ArmSettings settings;
ArmData state;

double motorPIDOut = 0.0;
double motorCurrent = 0.0;
double currentDistance = 0.0;
double targetDistance = 6.0;
double pwmValue = 0.0;

#endif // STATE_H