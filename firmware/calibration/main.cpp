// Bench calibration: drives the carriage through set points and prints tables
// to pair with dial-probe readings. Commands are typed into the serial monitor.

#include "magnet.h"
#include "motor.h"
#include <Arduino.h>

constexpr float NEUTRAL_MM = 6.0f;
constexpr float SWEEP_START_MM = 3.0f;
constexpr float SWEEP_END_MM = 10.0f;
const float SWEEP_STEP_MM = 0.5f;

const uint32_t STABLE_TIME_MS = 200; // in the deadband this long = arrived
const uint32_t SETTLE_TIMEOUT_MS = 5000;
const uint32_t HOLD_TIME_MS = 2000;     // pause at each set point for the probe
const uint32_t SAMPLE_WINDOW_MS = 1000; // average the end of the hold
const uint32_t PROBE_RESET_MS = 10000;  // extra hold on the first point
const uint32_t DEBUG_INTERVAL_MS = 200;

const int DEFAULT_REPEATS = 10;
const int MAX_REPEATS = 50;
const int MAX_ROWS = 2 * MAX_REPEATS + 1;

// PID step test from neutral: big and small moves in both directions, ending
// back at neutral
constexpr float PID_STEPS_MM[] = {3.0f, 10.0f, 6.0f, 6.5f, 6.0f};
const int PID_STEP_COUNT = sizeof(PID_STEPS_MM) / sizeof(PID_STEPS_MM[0]);
const uint32_t PID_SAMPLE_MS = 20;         // log period
const uint32_t PID_SETTLED_MS = 1000;      // in the deadband this long = done
const uint32_t PID_STEP_TIMEOUT_MS = 6000; // gives up on a step after this
const uint32_t PID_REST_MS = 500;          // still at neutral before step 1
const int MAX_PID_SAMPLES =
    PID_STEP_COUNT * (PID_STEP_TIMEOUT_MS / PID_SAMPLE_MS + 1);

// Pulse test: open-loop pulses from mid-travel, fast then at the minimum PWM,
// each way, so the moves roughly cancel out
constexpr float PULSE_START_MM = 6.5f;

// Targets outside the travel get clamped, so the tests would wait for a
// distance the motor never goes to
static_assert(SWEEP_START_MM >= TRAVEL_MIN_MM && SWEEP_END_MM <= TRAVEL_MAX_MM,
              "M and R sweep outside TRAVEL_MIN_MM / TRAVEL_MAX_MM");
static_assert(NEUTRAL_MM >= TRAVEL_MIN_MM && NEUTRAL_MM <= TRAVEL_MAX_MM,
              "neutral outside TRAVEL_MIN_MM / TRAVEL_MAX_MM");
constexpr bool inTravel(const float *targets, int count) {
  return count == 0 || (targets[0] >= TRAVEL_MIN_MM &&
                        targets[0] <= TRAVEL_MAX_MM &&
                        inTravel(targets + 1, count - 1));
}
static_assert(inTravel(PID_STEPS_MM, PID_STEP_COUNT),
              "PID step test outside TRAVEL_MIN_MM / TRAVEL_MAX_MM");
static_assert(PULSE_START_MM - 1.0f >= TRAVEL_MIN_MM &&
                  PULSE_START_MM + 1.0f <= TRAVEL_MAX_MM,
              "pulse test start too close to TRAVEL_MIN_MM / TRAVEL_MAX_MM");
const float PULSE_HIGH_PWM = MOTOR_MIN_PWM + 100.0f;
const float PULSE_PWMS[] = {PULSE_HIGH_PWM, -PULSE_HIGH_PWM, MOTOR_MIN_PWM,
                            -MOTOR_MIN_PWM};
const int PULSE_COUNT = sizeof(PULSE_PWMS) / sizeof(PULSE_PWMS[0]);
const uint32_t PULSE_MS = 250;
const uint32_t PULSE_FIT_FROM_MS = 125; // steady speed from here to the end
const uint32_t PULSE_REST_MS = 500;     // braked between pulses
const float PULSE_MARGIN_MM = 1.0f;     // aborts this close to the travel ends
const float PULSE_STILL_MM_S = 0.2f;    // slower than this didn't move
// No overshoot for a lagging integrator needs Kp * K * delay below 1/e;
// a closed-loop time constant of twice the delay gives 1/3
const float PULSE_TUNE_FACTOR = 2.0f;

struct Sample {
  float zmT;
  float distanceMM; // raw, not the rolling median
  uint32_t timeMs;  // when the read finished
};

struct Row {
  float setMM;
  float zmT;
  float distanceMM;
  int32_t timeToTargetMs; // -1 if it never settled
};

struct PidSample {
  uint8_t step;
  uint16_t timeMs;  // since the step's target was set
  float distanceMM; // the rolling median the PID runs on
  float pwm;        // signed, positive drives towards larger distances
  float currentMA;
};

struct Gains {
  double kp;
  double ki;
  double kd;
};

struct Pulse {
  float pwm;
  float speedMMs; // steady speed, signed
  float lagMs;    // from the command until the steady-speed line starts
  float readMs;   // mean time between magnet reads
};

QueueHandle_t sampleQueue; // every magnet read, for averaging during a hold
QueueHandle_t gainsQueue;  // new PID gains for the motor task to apply
volatile float targetRequest = NEUTRAL_MM;
volatile bool magnetReady = false;
volatile bool pidEnabled = true; // off while a test drives the motor itself
Row rows[MAX_ROWS];
PidSample pidSamples[MAX_PID_SAMPLES];

static void sensingTask(void *parameter) {
  while (!magnet.begin(MAG_SDA_PIN, MAG_SCL_PIN)) {
    Serial.println("Error: MLX90393 magnetometer not detected, retrying...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  while (true) {
    motor.readCurrent();
    if (magnet.read()) {
      Sample sample = {magnet.zmT, magnet.rawDistanceMM, millis()};
      xQueueSend(sampleQueue, &sample, 0); // dropped when nobody is sampling
      magnetReady = true;
    } else {
      Serial.println("Failed to read sensor.");
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// The PID only runs on this task, so targets are handed over via targetRequest
// and gains via gainsQueue. It steps once per magnet reading, so the
// derivative never sees a repeated distance; if the readings stop, so does the
// motor.
static void motorTask(void *parameter) {
  motor.begin();
  while (!magnetReady)
    vTaskDelay(pdMS_TO_TICKS(10));
  magnet.readingTask = xTaskGetCurrentTaskHandle();

  Gains gains;
  bool paused = false;
  while (true) {
    bool reading = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MAGNET_TIMEOUT_MS));
    if (xQueueReceive(gainsQueue, &gains, 0) == pdTRUE)
      motor.setPIDCalibration(gains.kp, gains.ki, gains.kd);
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

void printMenu() {
  Serial.println();
  Serial.println("Calibration commands:");
  Serial.println("  P   PID step test, 6 > 3 > 10 > 6 > 6.5 > 6 mm");
  Serial.println("      (P 3 0.1 1 sets Kp Ki Kd first, kept until reset)");
  Serial.println("  C   motor current (not implemented yet)");
  Serial.println("  M   magnetometer sweep, 3-10 mm in 0.5 mm steps");
  Serial.println("  R   repeatability, 3 <-> 10 mm x10 (R5 = 5 times)");
  Serial.println("  A   pulse test, measures the motor and suggests gains");
  Serial.printf("  #   go to that distance in mm, %.0f-%.0f, e.g. 7.5\n",
                TRAVEL_MIN_MM, TRAVEL_MAX_MM);
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
Row measureAt(float targetMM, uint32_t extraHoldMs = 0) {
  Serial.printf("-> %.2f mm\n", targetMM);
  Row row = {targetMM, 0.0f, 0.0f, moveTo(targetMM)};

  if (extraHoldMs > 0) {
    Serial.printf("Reset the probe, waiting %d s\n", extraHoldMs / 1000);
    waitWithDebug(extraHoldMs);
  }

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

// Ends every run's output; the UI takes the Temperature line as the end of
// the table
void printTrailer() {
  Serial.printf("\nTemperature: %.2f C\n", magnet.tempC);
  Serial.printf("Motor current: %.2f mA\n", motor.filteredCurrent);
  Serial.printf("Remanence: %.2f mT\n", magnet.remanenceMT);
  Serial.printf("Z offset: %.3f mm\n", magnet.zOffsetMM);
  Serial.printf("Calibration temperature: %.2f C\n", magnet.calibrationTempC);
  printMenu();
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
  printTrailer();
}

void runMagnetometer() {
  int steps = lround((SWEEP_END_MM - SWEEP_START_MM) / SWEEP_STEP_MM) + 1;
  for (int i = 0; i < steps; i++)
    rows[i] = measureAt(SWEEP_START_MM + i * SWEEP_STEP_MM,
                        i == 0 ? PROBE_RESET_MS : 0);
  finish(steps, false);
}

void runRepeatability(int repeats) {
  int rowCount = 0;
  rows[rowCount++] = measureAt(SWEEP_START_MM, PROBE_RESET_MS);
  for (int i = 0; i < repeats; i++) {
    rows[rowCount++] = measureAt(SWEEP_END_MM);
    rows[rowCount++] = measureAt(SWEEP_START_MM);
  }
  finish(rowCount, true);
}

// Logs one step every PID_SAMPLE_MS from the moment its target is set until
// it has sat in the deadband for PID_SETTLED_MS, or the timeout. Returns the
// new sample count.
int recordStep(int step, float targetMM, int sampleCount) {
  Serial.printf("-> %.2f mm\n", targetMM);
  targetRequest = targetMM;
  uint32_t start = millis();
  uint32_t settledSince = 0;
  bool inDeadband = false;
  TickType_t wake = xTaskGetTickCount();

  while (sampleCount < MAX_PID_SAMPLES) {
    uint32_t elapsed = millis() - start;
    float distanceMM = magnet.distanceMM;
    float pwm = motor.output < 0 ? -motor.pwmValue : motor.pwmValue;
    pidSamples[sampleCount++] = {(uint8_t)step, (uint16_t)elapsed, distanceMM,
                                 pwm, motor.filteredCurrent};

    if (fabs(distanceMM - targetMM) <= DISTANCE_DEADBAND) {
      if (!inDeadband)
        settledSince = elapsed;
      inDeadband = true;
      if (elapsed - settledSince >= PID_SETTLED_MS)
        break;
    } else {
      inDeadband = false;
    }

    if (elapsed >= PID_STEP_TIMEOUT_MS) {
      Serial.printf("WARNING: did not settle at %.2f mm\n", targetMM);
      break;
    }
    printDebug();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(PID_SAMPLE_MS));
  }
  return sampleCount;
}

// "P" keeps the current gains, "P 3 0.1 1" sets Kp Ki Kd first
bool parseGains(const char *text, Gains *gains) {
  double values[3];
  for (double &value : values) {
    char *end;
    value = strtod(text, &end);
    if (end == text || value < 0)
      return false;
    text = end;
  }
  while (isspace(*text))
    text++;
  if (*text != '\0')
    return false;

  *gains = {values[0], values[1], values[2]};
  return true;
}

void runPid(const String &command) {
  if (command.length() > 1) {
    Gains gains;
    if (!parseGains(command.c_str() + 1, &gains)) {
      Serial.println("Use P, or P <Kp> <Ki> <Kd> with gains of 0 or more");
      return;
    }
    xQueueOverwrite(gainsQueue, &gains); // applied before the move below
  }

  Serial.printf("-> %.2f mm (neutral)\n", NEUTRAL_MM);
  moveTo(NEUTRAL_MM);
  waitWithDebug(PID_REST_MS);

  int sampleCount = 0;
  for (int i = 0; i < PID_STEP_COUNT; i++)
    sampleCount = recordStep(i + 1, PID_STEPS_MM[i], sampleCount);

  // The last step is back to neutral, so no park before the table
  Serial.println();
  Serial.printf("PID step test  Kp %.4f  Ki %.4f  Kd %.4f  Deadband %.3f mm  "
                "Travel %.2f-%.2f mm\n",
                motor.pid.GetKp(), motor.pid.GetKi(), motor.pid.GetKd(),
                DISTANCE_DEADBAND, TRAVEL_MIN_MM, TRAVEL_MAX_MM);
  Serial.println(
      "Step  Target (mm)  Time (ms)  Distance (mm)  PWM    Current (mA)");
  for (int i = 0; i < sampleCount; i++) {
    const PidSample &sample = pidSamples[i];
    Serial.printf("%-4d  %-11.2f  %-9u  %-13.3f  %-5.0f  %.1f\n", sample.step,
                  PID_STEPS_MM[sample.step - 1], sample.timeMs,
                  sample.distanceMM, sample.pwm, sample.currentMA);
  }
  printTrailer();
}

// One open-loop pulse at this signed PWM from rest, then braked. Fits a line to
// the raw readings over the end of the pulse: its slope is the steady speed,
// and where it crosses the starting distance is the lag. Returns why it
// failed, or nullptr.
const char *runPulse(float pwm, Pulse *pulse) {
  Sample sample;
  xQueueReset(sampleQueue);
  float startMM = 0.0f;
  int startCount = 0;
  uint32_t settle = millis();
  while (millis() - settle < 100)
    if (xQueueReceive(sampleQueue, &sample, pdMS_TO_TICKS(10)) == pdTRUE) {
      startMM += sample.distanceMM;
      startCount++;
    }
  if (startCount == 0)
    return "no magnet readings";
  startMM /= startCount;

  // Least squares sums over the fit window, time in seconds from the command
  double sumT = 0, sumD = 0, sumTT = 0, sumTD = 0;
  int fitCount = 0;
  uint32_t firstRead = 0, lastRead = 0;
  int reads = 0;
  bool safe = true;

  uint32_t start = millis();
  motor.setPWM(pwm);
  while (millis() - start < PULSE_MS) {
    if (xQueueReceive(sampleQueue, &sample, pdMS_TO_TICKS(10)) != pdTRUE)
      continue;
    if ((int32_t)(sample.timeMs - start) < 0)
      continue; // read before the pulse began
    if (sample.distanceMM < TRAVEL_MIN_MM + PULSE_MARGIN_MM ||
        sample.distanceMM > TRAVEL_MAX_MM - PULSE_MARGIN_MM) {
      safe = false;
      break;
    }
    if (reads++ == 0)
      firstRead = sample.timeMs;
    lastRead = sample.timeMs;

    uint32_t elapsedMs = sample.timeMs - start;
    if (elapsedMs >= PULSE_FIT_FROM_MS) {
      double t = elapsedMs / 1000.0;
      sumT += t;
      sumD += sample.distanceMM;
      sumTT += t * t;
      sumTD += t * sample.distanceMM;
      fitCount++;
    }
  }
  motor.stop();

  if (!safe)
    return "stopped within 1 mm of the travel ends";
  waitWithDebug(PULSE_REST_MS);
  if (fitCount < 3 || reads < 2)
    return "too few magnet readings during a pulse";

  double slope =
      (fitCount * sumTD - sumT * sumD) / (fitCount * sumTT - sumT * sumT);
  double intercept = (sumD - slope * sumT) / fitCount;
  pulse->pwm = pwm;
  pulse->speedMMs = slope;
  pulse->lagMs = fabs(slope) < PULSE_STILL_MM_S
                     ? NAN
                     : (startMM - intercept) / slope * 1000.0;
  pulse->readMs = (float)(lastRead - firstRead) / (reads - 1);
  return nullptr;
}

// Fits the carriage as speed = K * PID output after a delay, the speed itself
// lagging by the motor's time constant, then tunes PD for no overshoot:
// Kp = 1 / (K * (tc + delay)) with tc = PULSE_TUNE_FACTOR * delay, and Kd
// cancelling the motor lag. Ki stays 0; the deadband resets it on arrival
// anyway, and on an integrating process it only adds overshoot.
void runPulseTest() {
  Serial.printf("-> %.2f mm (start)\n", PULSE_START_MM);
  moveTo(PULSE_START_MM);
  waitWithDebug(PID_REST_MS);

  pidEnabled = false;
  vTaskDelay(pdMS_TO_TICKS(100)); // the motor task finishes its last update
  motor.stop();

  Pulse pulses[PULSE_COUNT];
  const char *failure = nullptr;
  for (int i = 0; i < PULSE_COUNT && !failure; i++) {
    Serial.printf("Pulse %+.0f PWM\n", PULSE_PWMS[i]);
    failure = runPulse(PULSE_PWMS[i], &pulses[i]);
  }
  pidEnabled = true;

  Serial.printf("-> %.2f mm (neutral)\n", NEUTRAL_MM);
  moveTo(NEUTRAL_MM);
  if (failure) {
    Serial.println("\nPulse test  Failed");
    Serial.printf("WARNING: %s, no gains suggested\n", failure);
    printTrailer();
    return;
  }

  // Pulses 0/1 are fast up/down, 2/3 at the minimum PWM up/down
  float readMs = 0.0f;
  for (const Pulse &pulse : pulses)
    readMs += pulse.readMs / PULSE_COUNT;
  float speedPerPWM = 0.0f; // the faster direction, to be safe
  float creepMMs = 0.0f;
  float lagMs = 0.0f;
  for (int up = 0; up < 2; up++) {
    float fast = fabs(pulses[up].speedMMs);
    float creep = fabs(pulses[2 + up].speedMMs);
    speedPerPWM =
        fmax(speedPerPWM, (fast - creep) / (PULSE_HIGH_PWM - MOTOR_MIN_PWM));
    creepMMs = fmax(creepMMs, creep);
    if (!isnan(pulses[up].lagMs))
      lagMs = fmax(lagMs, pulses[up].lagMs);
  }

  // A raw read lags the carriage by about half a read; the rolling median
  // the PID runs on adds (N - 1) / 2 reads. The rest of the lag is the motor.
  float sensingMs = readMs * DISTANCE_AVERAGE_COUNT / 2.0f;
  float motorLagMs = fmax(0.0f, lagMs - readMs / 2.0f);
  float delayS = sensingMs / 1000.0f;

  Gains gains = {0.0, 0.0, 0.0};
  bool tuned = speedPerPWM > 0.0f;
  if (tuned) {
    gains.kp = 1.0 / (speedPerPWM * (PULSE_TUNE_FACTOR + 1.0f) * delayS);
    // PID_v1 assumes Compute runs every PID_SAMPLE_TIME_MS, not every read
    gains.kd = gains.kp * motorLagMs / 1000.0 * PID_SAMPLE_TIME_MS / readMs;
  }

  Serial.println();
  Serial.printf("Pulse test  Read period %.1f ms\n", readMs);
  Serial.println("PWM   Speed (mm/s)  Lag (ms)");
  for (const Pulse &pulse : pulses) {
    Serial.printf("%-4.0f  %-12.3f  ", pulse.pwm, pulse.speedMMs);
    if (isnan(pulse.lagMs))
      Serial.println("-");
    else
      Serial.printf("%.0f\n", pulse.lagMs);
  }
  Serial.printf("Speed per PWM: %.4f mm/s\n", speedPerPWM);
  Serial.printf("Creep at minimum PWM: %.3f mm/s\n", creepMMs);
  Serial.printf("Motor lag: %.0f ms\n", motorLagMs);
  Serial.printf("Sensing delay: %.0f ms\n", sensingMs);

  float creepOvershootMM = creepMMs * (sensingMs + motorLagMs) / 1000.0f;
  if (creepMMs < PULSE_STILL_MM_S)
    Serial.println(
        "WARNING: didn't move at the minimum PWM, raise MOTOR_MIN_PWM");
  else if (creepOvershootMM > DISTANCE_DEADBAND)
    Serial.printf("WARNING: creeping at the minimum PWM alone overshoots "
                  "~%.2f mm, past the deadband; lower MOTOR_MIN_PWM\n",
                  creepOvershootMM);

  if (tuned) {
    Serial.printf("Suggested gains  Kp %.4f  Ki %.4f  Kd %.4f\n", gains.kp,
                  gains.ki, gains.kd);
    Serial.printf("Try them with: P %.4f %.4f %.4f\n", gains.kp, gains.ki,
                  gains.kd);
  } else {
    Serial.println(
        "WARNING: no faster at high PWM than at the minimum, no gains");
  }
  printTrailer();
}

void setup() {
  Serial.begin(115200);
  sampleQueue = xQueueCreate(16, sizeof(Sample));
  gainsQueue = xQueueCreate(1, sizeof(Gains));

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

  if (isDigit(command[0]) || command[0] == '.') {
    float targetMM = command.toFloat();
    // The motor would clamp it, and moveTo would wait for a distance it never
    // reaches
    if (targetMM < TRAVEL_MIN_MM || targetMM > TRAVEL_MAX_MM) {
      Serial.printf("Target must be %.2f-%.2f mm\n", TRAVEL_MIN_MM,
                    TRAVEL_MAX_MM);
      return;
    }
    Serial.printf("-> %.2f mm\n", targetMM);
    int32_t timeToTargetMs = moveTo(targetMM);
    if (timeToTargetMs >= 0)
      Serial.printf("Arrived in %d ms, reading %.3f mm\n", timeToTargetMs,
                    magnet.distanceMM);
    return;
  }

  switch (command[0]) {
  case 'P':
    runPid(command);
    break;
  case 'C':
    Serial.println("Motor current calibration is not implemented yet.");
    break;
  case 'M':
    runMagnetometer();
    break;
  case 'A':
    runPulseTest();
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
