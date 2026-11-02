#include "calibration.h"
#include "comms.h"
#include "magnet.h"
#include "motor.h"
#include <Arduino.h>

// Doesn't print, see failedReads in calibration.h
static void sensingTask(void *parameter) {
  while (!magnet.begin(MAG_SDA_PIN, MAG_SCL_PIN))
    vTaskDelay(pdMS_TO_TICKS(1000)); // setup() reports it

  while (true) {
    ArmCalibration calibration;
    if (xQueueReceive(magnetCalibrationQueue, &calibration, 0) == pdTRUE)
      magnet.setCalibration(calibration.remanenceMT, calibration.zOffsetMM,
                            calibration.calibrationTempC);

    motor.readCurrent();
    if (magnet.read()) {
      Sample sample = {magnet.zmT, magnet.rawDistanceMM, millis()};
      xQueueSend(sampleQueue, &sample, 0); // dropped when nobody is sampling
      magnetReady = true;
    } else {
      failedReads++; // no response, or dropped as implausible
    }
    // readData() already blocks ~25 ms for the conversion, so only yield here
    // to keep the read period (and the rolling median's lag) short
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// The PID only runs on this task, so targets are handed over via targetRequest
// and gains via motorCalibrationQueue. It steps once per magnet reading, so the
// derivative never sees a repeated distance; if the readings stop, so does the
// motor.
static void motorTask(void *parameter) {
  motor.begin();
  while (!magnetReady)
    vTaskDelay(pdMS_TO_TICKS(10));
  magnet.readingTask = xTaskGetCurrentTaskHandle();

  ArmCalibration calibration;
  bool paused = false;
  while (true) {
    bool reading = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MAGNET_TIMEOUT_MS));
    if (xQueueReceive(motorCalibrationQueue, &calibration, 0) == pdTRUE) {
      motor.minPWM = calibration.minPWM;
      motor.setPIDCalibration(calibration.kp, calibration.ki, calibration.kd);
    }
    if (!pidEnabled) {
      paused = true;
      continue;
    }
    // Coming back from a test, the PID's history is stale, so start afresh
    if (paused || targetRequest != (float)motor.setpoint) {
      motor.setTarget(targetRequest);
      paused = false;
    }
    if (reading)
      motor.update(magnet.distanceMM);
    else
      motor.stop();
  }
}

void setup() {
  setupComms();
  // This board's row from defaults.h, before the tasks start using it
  activeCalibration = findCalibration(macAddress());
  const ArmCalibration &calibration = activeCalibration;
  motor.minPWM = calibration.minPWM;
  motor.setPIDCalibration(calibration.kp, calibration.ki, calibration.kd);
  magnet.setCalibration(calibration.remanenceMT, calibration.zOffsetMM,
                        calibration.calibrationTempC);
  printCalibration(calibration);

  sampleQueue = xQueueCreate(16, sizeof(Sample));
  motorCalibrationQueue = xQueueCreate(1, sizeof(ArmCalibration));
  magnetCalibrationQueue = xQueueCreate(1, sizeof(ArmCalibration));

  xTaskCreatePinnedToCore(sensingTask, "sensing", 10000, nullptr, 1, nullptr,
                          0);
  xTaskCreatePinnedToCore(motorTask, "motor", 2048, nullptr, 1, nullptr, 1);

  uint32_t waitingSince = millis();
  while (!magnetReady) {
    if (millis() - waitingSince >= 1000) {
      Serial.println("Error: MLX90393 magnetometer not detected, retrying...");
      waitingSince = millis();
    }
    delay(10);
  }
  Serial.printf("-> %.2f mm (neutral)\n", NEUTRAL_MM);
  moveTo(NEUTRAL_MM);
  printMenu();
}

// Serial commands (targets and the calibration tests) from calibration.h
void loop() { handleSerial(); }
