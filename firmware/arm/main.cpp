#include "motor.h"
#include <Arduino.h>

TaskHandle_t SensingTask;
TaskHandle_t MovementTask;

static void motorTask(void *parameter) {
  setupMotor();

  while (true) {
    handlePID();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void sensingTaskCode(void *parameter) {
  while (!setupMagnet()) {
    Serial.println("Error: MLX90393 magnetometer not detected, retrying...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  while (true) {
    float currentCurrent = readMotorCurrent();
    readMagnet();
    printMagnet();

    Serial.print(">Motor Current:");
    Serial.print(currentCurrent, 2);
    Serial.println(" mA");

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);

  xTaskCreatePinnedToCore(sensingTaskCode, "sensing", 10000, NULL, 1,
                          &SensingTask, 0);
  // xTaskCreatePinnedToCore(motorTask, "motor", 2048, nullptr, 1, MovementTask,
  // 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }