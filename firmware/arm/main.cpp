#include "calibration.h"
#include "comms.h"
#include "magnet.h"
#include "motor.h"
#include <Arduino.h>

static void sensingTask(void *parameter) {
  while (true) {
    motor.readCurrent();
    magnet.read();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void motorTask(void *parameter) {
  motor.begin();
  motor.target = NEUTRAL_MM;

  while (true)
    motor.update(magnet.distanceMM);
}

void setup() {
  setupComms();
  setupCalibration();
  setupMagnet();

  xTaskCreatePinnedToCore(sensingTask, "sensing", 10000, nullptr, 1, nullptr,
                          0);
  xTaskCreatePinnedToCore(motorTask, "motor", 2048, nullptr, 1, nullptr, 1);
  printMenu();
}

void loop() {
  handleSerial();
  printStatus();
  vTaskDelay(pdMS_TO_TICKS(10));
}
