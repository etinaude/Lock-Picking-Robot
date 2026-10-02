// Bench calibration: drives the carriage through set points and prints tables
// to pair with dial-probe readings. Commands are typed into the serial monitor.

#include "magnet.h"
#include "motor.h"
#include <Arduino.h>

const float NEUTRAL_MM = 6.0f;
const float SWEEP_START_MM = 3.0f;
const float SWEEP_END_MM = 10.0f;
const float SWEEP_STEP_MM = 0.5f;

const uint32_t STABLE_TIME_MS = 200; // in the deadband this long = arrived
const uint32_t SETTLE_TIMEOUT_MS = 5000;
const uint32_t HOLD_TIME_MS = 2000;     // pause at each set point for the probe
const uint32_t SAMPLE_WINDOW_MS = 1000; // average the end of the hold
const uint32_t DEBUG_INTERVAL_MS = 200;

const int DEFAULT_REPEATS = 10;
const int MAX_REPEATS = 50;
const int MAX_ROWS = 2 * MAX_REPEATS + 1;

struct Sample {
  float zmT;
  float distanceMM;
};

struct Row {
  float setMM;
  float zmT;
  float distanceMM;
  int32_t timeToTargetMs; // -1 if it never settled
};

QueueHandle_t sampleQueue; // every magnet read, for averaging during a hold
volatile float targetRequest = NEUTRAL_MM;
volatile bool magnetReady = false;
Row rows[MAX_ROWS];

static void sensingTask(void *parameter) {
  while (!magnet.begin(MAG_SDA_PIN, MAG_SCL_PIN)) {
    Serial.println("Error: MLX90393 magnetometer not detected, retrying...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  while (true) {
    motor.readCurrent();
    if (magnet.read()) {
      Sample sample = {magnet.zmT, magnet.rawDistanceMM};
      xQueueSend(sampleQueue, &sample, 0); // dropped when nobody is sampling
      magnetReady = true;
    } else {
      Serial.println("Failed to read sensor.");
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// The PID only runs on this task, so targets are handed over via targetRequest
static void motorTask(void *parameter) {
  motor.begin();
  while (!magnetReady)
    vTaskDelay(pdMS_TO_TICKS(10));

  while (true) {
    if (targetRequest != (float)motor.setpoint)
      motor.setTarget(targetRequest);
    motor.update(magnet.distanceMM);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void printMenu() {
  Serial.println();
  Serial.println("Calibration commands:");
  Serial.println("  P   PID tuning (not implemented yet)");
  Serial.println("  C   motor current (not implemented yet)");
  Serial.println("  M   magnetometer sweep, 3-10 mm in 0.5 mm steps");
  Serial.println("  R   repeatability, 3 <-> 10 mm x10 (R5 = 5 times)");
}

void printDebug() {
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint < DEBUG_INTERVAL_MS)
    return;
  lastPrint = millis();

  Serial.printf(">targetDistance:%.3f\n", motor.setpoint);
  Serial.printf(">currentDistance:%.3f\n", magnet.distanceMM);
  Serial.printf(">B_z:%.4f\n", magnet.zmT);
  Serial.printf(">pwmValue:%.2f\n", motor.pwmValue);
  Serial.printf(">Motor Current:%.2f\n", motor.filteredCurrent);
  Serial.printf(">Magnet Temp:%.2f\n", magnet.tempC);
}

void waitWithDebug(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    printDebug();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Returns ms from the command until it entered the deadband for good, or -1
int32_t moveTo(float targetMM) {
  targetRequest = targetMM;
  uint32_t start = millis();
  uint32_t stableSince = 0;
  bool inDeadband = false;

  while (true) {
    printDebug();
    uint32_t now = millis();

    if (fabs(magnet.distanceMM - targetMM) <= DISTANCE_DEADBAND) {
      if (!inDeadband)
        stableSince = now;
      inDeadband = true;
      if (now - stableSince >= STABLE_TIME_MS)
        return stableSince - start;
    } else {
      inDeadband = false;
    }

    if (now - start >= SETTLE_TIMEOUT_MS) {
      Serial.printf("WARNING: did not settle at %.2f mm\n", targetMM);
      return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Move, hold for the probe, and average the readings from the end of the hold
Row measureAt(float targetMM) {
  Serial.printf("-> %.2f mm\n", targetMM);
  Row row = {targetMM, 0.0f, 0.0f, moveTo(targetMM)};

  waitWithDebug(HOLD_TIME_MS - SAMPLE_WINDOW_MS);
  xQueueReset(sampleQueue);

  uint32_t start = millis();
  int count = 0;
  Sample sample;
  while (millis() - start < SAMPLE_WINDOW_MS) {
    printDebug();
    if (xQueueReceive(sampleQueue, &sample, pdMS_TO_TICKS(10)) == pdTRUE) {
      row.zmT += sample.zmT;
      row.distanceMM += sample.distanceMM;
      count++;
    }
  }

  if (count > 0) {
    row.zmT /= count;
    row.distanceMM /= count;
  }
  return row;
}

// Park at neutral first so the table is the last thing printed
void finish(int rowCount, bool withTime) {
  Serial.printf("-> %.2f mm (neutral)\n", NEUTRAL_MM);
  moveTo(NEUTRAL_MM);

  Serial.println();
  Serial.print("Set Distance (mm)  Magno reading(mT)  Predicted distance (mm)");
  if (withTime)
    Serial.print("  time to target (ms)");
  Serial.println();

  for (int i = 0; i < rowCount; i++) {
    Serial.printf("%-17.2f  %-17.4f  %-23.3f", rows[i].setMM, rows[i].zmT,
                  rows[i].distanceMM);
    if (withTime) {
      if (rows[i].timeToTargetMs < 0)
        Serial.print("  timeout");
      else
        Serial.printf("  %d", rows[i].timeToTargetMs);
    }
    Serial.println();
  }

  Serial.printf("\nTemperature: %.2f C\n", magnet.tempC);
  printMenu();
}

void runMagnetometer() {
  int steps = lround((SWEEP_END_MM - SWEEP_START_MM) / SWEEP_STEP_MM) + 1;
  for (int i = 0; i < steps; i++)
    rows[i] = measureAt(SWEEP_START_MM + i * SWEEP_STEP_MM);
  finish(steps, false);
}

void runRepeatability(int repeats) {
  int rowCount = 0;
  rows[rowCount++] = measureAt(SWEEP_START_MM);
  for (int i = 0; i < repeats; i++) {
    rows[rowCount++] = measureAt(SWEEP_END_MM);
    rows[rowCount++] = measureAt(SWEEP_START_MM);
  }
  finish(rowCount, true);
}

void setup() {
  Serial.begin(115200);
  sampleQueue = xQueueCreate(16, sizeof(Sample));

  xTaskCreatePinnedToCore(sensingTask, "sensing", 10000, nullptr, 1, nullptr,
                          0);
  xTaskCreatePinnedToCore(motorTask, "motor", 2048, nullptr, 1, nullptr, 1);

  while (!magnetReady)
    delay(10);
  Serial.printf("-> %.2f mm (neutral)\n", NEUTRAL_MM);
  moveTo(NEUTRAL_MM);
  printMenu();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toUpperCase();
  if (command.isEmpty())
    return;

  switch (command[0]) {
  case 'P':
    Serial.println("PID calibration is not implemented yet.");
    break;
  case 'C':
    Serial.println("Motor current calibration is not implemented yet.");
    break;
  case 'M':
    runMagnetometer();
    break;
  case 'R': {
    int repeats =
        command.length() > 1 ? command.substring(1).toInt() : DEFAULT_REPEATS;
    if (repeats < 1 || repeats > MAX_REPEATS) {
      Serial.printf("Repeat count must be 1-%d\n", MAX_REPEATS);
      break;
    }
    runRepeatability(repeats);
    break;
  }
  default:
    printMenu();
  }

  while (Serial.available()) // drop anything typed during a run
    Serial.read();
}
