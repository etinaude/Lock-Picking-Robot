#include "comms.h"
#include "motor.h"
#include <Arduino.h>

TaskHandle_t SensingTask;
TaskHandle_t MovementTask;

static void motorTaskCode(void *parameter) {
  setupMotor();

  while (true) {
    handlePID();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void sensingTaskCode(void *parameter) {
  while (!setupMagnet()) {
    Serial.println("Error: MLX90393 magnetometer not detected, retrying...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  while (true) {
    readMotorCurrent();
    readMagnet();
    printStatus();
    receiveSerial();

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  setupComms();

  xTaskCreatePinnedToCore(sensingTaskCode, "sensing", 10000, NULL, 1,
                          &SensingTask, 0);
  xTaskCreatePinnedToCore(motorTaskCode, "motor", 2048, nullptr, 1,
                          &MovementTask, 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }