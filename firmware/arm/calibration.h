#pragma once

#include "defaults.h"
#include "magnet.h"
#include "motor.h"
#include <Arduino.h>
#include <RunningAverage.h>

const float NEUTRAL_MM = 6.0f;
const float SWEEP_START_MM = 3.0f;
const float SWEEP_END_MM = 10.0f;
const float SWEEP_STEP_MM = 0.5f;

const int STATUS_INTERVAL_MS = 200;
const int STABLE_TIME_MS = 200;
const int SETTLE_TIMEOUT_MS = 5000;
const int HOLD_TIME_MS = 2000;
const int SAMPLE_WINDOW_MS = 1000;
const int PROBE_RESET_MS = 10000;
const int32_t NOT_SETTLED = -1;

const int DEFAULT_REPEATS = 10;
const int MAX_REPEATS = 50;
const int MAX_ROWS = 2 * MAX_REPEATS + 1;

RunningAverage aveField(20);

struct Row {
  float setMM;
  float zmT;
  float distanceMM;
  int32_t timeToTargetMs;
};

ArmCalibration activeCalibration = DEFAULT_CALIBRATION;
uint32_t lastStatus = 0;

Row rows[MAX_ROWS];

void printStatus() {
  if (millis() - lastStatus < STATUS_INTERVAL_MS)
    return;
  lastStatus = millis();

  Serial.printf(">targetDistance:%.3f\n", motor.target);
  Serial.printf(">currentDistance:%.3f\n", magnet.distanceMM);
  Serial.printf(">B_z:%.4f\n", magnet.zmT);
  Serial.printf(">pwmValue:%.2f\n", motor.pwmValue);
  Serial.printf(">motor PID:%.4f\n", motor.output);
  Serial.printf(">Motor Current:%.2f\n", motor.filteredCurrent);
  Serial.printf(">Magnet Temp:%.2f\n", magnet.tempC);
}

void waitWithStatus(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    printStatus();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

int32_t moveTo(float targetMM) {
  motor.target = targetMM;
  uint32_t commandedAt = millis();
  uint32_t enteredDeadbandAt = 0;
  bool inDeadband = false;

  while (true) {
    printStatus();
    uint32_t now = millis();

    if (fabs(magnet.distanceMM - targetMM) <= DISTANCE_DEADBAND) {
      if (!inDeadband)
        enteredDeadbandAt = now;
      inDeadband = true;
      if (now - enteredDeadbandAt >= STABLE_TIME_MS)
        return enteredDeadbandAt - commandedAt;
    } else {
      inDeadband = false;
    }

    if (now - commandedAt >= SETTLE_TIMEOUT_MS) {
      Serial.printf("WARNING: did not settle at %.2f mm\n", targetMM);
      return NOT_SETTLED;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void park() {
  Serial.printf("-> %.2f mm (neutral)\n", NEUTRAL_MM);
  moveTo(NEUTRAL_MM);
}

float averageField(uint32_t windowMs) {
  uint32_t seenReadAt = magnet.lastGoodRead;
  uint32_t start = millis();
  while (millis() - start < windowMs) {
    printStatus();
    vTaskDelay(pdMS_TO_TICKS(1));
    if (magnet.lastGoodRead == seenReadAt)
      continue;
    seenReadAt = magnet.lastGoodRead;
    aveField.add(magnet.zmT);
  }
  return aveField.getAverage();
}

Row measureAt(float targetMM, int probeResetMs = 0) {
  Serial.printf("-> %.2f mm\n", targetMM);
  int32_t timeToTargetMs = moveTo(targetMM);

  if (probeResetMs > 0) {
    Serial.printf("Reset the probe, waiting %d s\n", probeResetMs / 1000);
    waitWithStatus(probeResetMs);
  }

  waitWithStatus(HOLD_TIME_MS - SAMPLE_WINDOW_MS);
  float zmT = averageField(SAMPLE_WINDOW_MS);
  return {targetMM, zmT, magnet.distanceFor(zmT), timeToTargetMs};
}

// The UI takes the Temperature line as the end of the table
void printTrailer() {
  Serial.printf("\nTemperature: %.2f C\n", magnet.tempC);
  Serial.printf("Motor current: %.2f mA\n", motor.filteredCurrent);
  Serial.printf("Remanence: %.2f mT\n", magnet.remanenceMT);
  Serial.printf("Z offset: %.3f mm\n", magnet.zOffsetMM);
  Serial.printf("Calibration temperature: %.2f C\n", magnet.calibrationTempC);
  printCalibration(activeCalibration);
}

void printRows(int rowCount, bool withTimeToTarget) {
  Serial.println();
  Serial.print("Set Distance (mm)  Magno reading(mT)  Predicted distance (mm)");
  if (withTimeToTarget)
    Serial.print("  time to target (ms)");
  Serial.println();

  for (int i = 0; i < rowCount; i++) {
    Serial.printf("%-17.2f  %-17.4f  %-23.3f", rows[i].setMM, rows[i].zmT,
                  rows[i].distanceMM);
    if (withTimeToTarget) {
      if (rows[i].timeToTargetMs == NOT_SETTLED)
        Serial.print("  timeout");
      else
        Serial.printf("  %d", rows[i].timeToTargetMs);
    }
    Serial.println();
  }
}

void runMagnetometer() {
  int pointCount = lround((SWEEP_END_MM - SWEEP_START_MM) / SWEEP_STEP_MM) + 1;
  for (int i = 0; i < pointCount; i++)
    rows[i] = measureAt(SWEEP_START_MM + i * SWEEP_STEP_MM,
                        i == 0 ? PROBE_RESET_MS : 0);
  park();
  printRows(pointCount, false);
  printTrailer();
}

void runRepeatability(int repeats) {
  if (repeats < 1 || repeats > MAX_REPEATS) {
    Serial.printf("Repeat count must be 1-%d\n", MAX_REPEATS);
    return;
  }

  int rowCount = 0;
  rows[rowCount++] = measureAt(SWEEP_START_MM, PROBE_RESET_MS);
  for (int i = 0; i < repeats; i++) {
    rows[rowCount++] = measureAt(SWEEP_END_MM);
    rows[rowCount++] = measureAt(SWEEP_START_MM);
  }
  park();
  printRows(rowCount, true);
  printTrailer();
}

void goTo(float targetMM) {
  if (targetMM < TRAVEL_MIN_MM || targetMM > TRAVEL_MAX_MM) {
    Serial.printf("Target must be %.2f-%.2f mm\n", TRAVEL_MIN_MM,
                  TRAVEL_MAX_MM);
    return;
  }
  Serial.printf("-> %.2f mm\n", targetMM);
  int32_t timeToTargetMs = moveTo(targetMM);
  if (timeToTargetMs != NOT_SETTLED)
    Serial.printf("Arrived in %d ms, reading %.3f mm\n", timeToTargetMs,
                  magnet.distanceMM);
}

void setCalibration(const ArmCalibration &calibration) {
  activeCalibration = calibration;
  motor.setCalibration(calibration.kp, calibration.ki, calibration.kd,
                       calibration.minPWM);
  magnet.setCalibration(calibration.remanenceMT, calibration.zOffsetMM,
                        calibration.calibrationTempC);
}

void setupCalibration() {
  setCalibration(findCalibration(macAddress()));
  printCalibration(activeCalibration);
}
