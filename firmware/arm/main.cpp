#include "calibration.h"
#include "comms.h"
#include "magnet.h"
#include "motor.h"
#include <Arduino.h>

static void sensingTask(void *parameter) {
  printMenu();
  while (true) {
    handleSerial();
    printStatus();
    motor.readCurrent();
    magnet.read();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void motorTask(void *parameter) {
  motor.begin();
  moveTo(NEUTRAL_MM);

  while (true) {
    if (pidEnabled)
      motor.update(magnet.distanceMM);
    else
      motor.stop();
  }
}

void setup() {
  setupComms();
  setupCalibration();
  setupMagnet();

  xTaskCreatePinnedToCore(sensingTask, "sensing", 10000, nullptr, 1, nullptr,
                          0);
  xTaskCreatePinnedToCore(motorTask, "motor", 2048, nullptr, 1, nullptr, 1);
}

// tasks handle loop
void loop() {}
