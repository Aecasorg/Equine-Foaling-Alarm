#include <MKRGSM.h>
#include <Wire.h>
#include <MPU6050_light.h>    // Library Manager: "MPU6050_light" by rfetick
#include "arduino_secrets.h"  // SIM PIN + alert number - copy arduino_secrets.h.example

#define PINNUMBER SECRET_PINNUMBER

GSM gsmAccess;
GSM_SMS sms;
MPU6050 mpu(Wire);

// ---- Hardware ----
const uint8_t BATTERY_PIN = A1;   // battery via 2:1 resistor divider (100k / 100k)
char numberToSMS[20] = SECRET_PHONE_NUMBER;
// (Tilt switches removed - the MPU now detects lying-on-side itself. Old pin D1 is free.)

// ===== FLAT DETECTION - CALIBRATE THIS =====
// HANGING_MOUNT true  = box dangles from the headcollar on a clip/strap.
//   Monitor the axis that reads ~1 g while the box hangs still; when she lies on
//   her side the box flops over and that reading COLLAPSES below SLACK_THRESHOLD_G.
//   (Box spin on the strap doesn't matter - the hang axis stays the hang axis.)
// HANGING_MOUNT false = box fixed flat against the cheekpiece.
//   Monitor the axis that reads ~0 g standing/grazing and ~1 g on her side;
//   flat = reading ABOVE FLAT_THRESHOLD_G.
// Calibrate either way with Serial Monitor @9600: hold the box in each pose and
// pick the axis (accX/accY/accZ) that behaves as described.
const boolean HANGING_MOUNT = true;
float monitoredAxisAccel() { return mpu.getAccX(); }  // <-- set after calibrating
const float FLAT_THRESHOLD_G  = 0.70;  // fixed mount: above this = on side
const float SLACK_THRESHOLD_G = 0.45;  // hanging mount: below this = on side

// ---- Trigger thresholds (seconds / deg-per-second) ----
const float MOTION_THRESHOLD    = 30.0; // gyro deg/s that counts as "real movement" (rest noise ~1-2)
const int   ACTIVE_SECS_LABOUR  = 8;    // seconds of movement WHILE FLAT => labour, alarm fast
const int   CALM_FLAT_BACKSTOP  = 1500; // flat & calm this long (25 min) => "check mare" safety net
const int   RESTLESS_EPISODES   = 3;    // up/down cycles within the window => early warning
const int   EPISODE_WINDOW_SECS = 1200; // 20 min window for counting restlessness

// ---- Battery / heartbeat ----
const float DIVIDER_RATIO           = 2.0;     // (R1+R2)/R2; 2.0 for two equal resistors
const float LOW_BATT_VOLTS          = 3.40;    // warn below this
const float BATT_RECOVER_VOLTS      = 3.70;    // re-arm the warning once charged past this
const long  HEARTBEAT_INTERVAL_SECS = 86400L;  // ~24 h (loop ticks ~1 s)

// ---- State ----
boolean notConnected  = true;
float   axisG         = 0.0;    // averaged |monitored axis| over the last sampled second
int  flatCount        = 0;      // seconds continuously flat this episode
int  activeWhileFlat  = 0;      // seconds of movement seen within this flat episode
boolean labourSent    = false;  // one lying-down alarm per flat episode
int  episodeCount     = 0;      // recent up/down cycles
int  windowTimer      = 0;      // counts the restlessness window down
boolean earlyWarnSent = false;

long    heartbeatTimer = 0;     // seconds since last heartbeat
int     lowBattStreak  = 0;     // consecutive low readings (rides out GSM-TX voltage sag)
boolean lowBattWarned  = false;

void gsmSetup() {
  while (notConnected) {
    if (gsmAccess.begin(PINNUMBER) == GSM_READY) {
      notConnected = false;
      blinkSignal();
    } else {
      delay(1000);
    }
  }
  gsmAccess.lowPowerMode();
}

void blinkOn() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);
  digitalWrite(LED_BUILTIN, LOW);
}

void blinkSignal() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
    delay(300);
  }
}

void sendSMS(const char* msg) {
  sms.beginSMS(numberToSMS);
  sms.print(msg);
  sms.endSMS();
}

// Averaged battery voltage through the divider.
float readBatteryVolts() {
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(2);
  }
  float raw = sum / 16.0;
  return raw * (3.3 / 1023.0) * DIVIDER_RATIO;
}

// Rough Li-ion state-of-charge (approximate; the curve is flat in the middle).
int batteryPercent(float v) {
  if (v >= 4.20) return 100;
  if (v <= 3.30) return 0;
  return (int)((v - 3.30) / (4.20 - 3.30) * 100.0);
}

// Sample the IMU for ~1 second. Returns PEAK movement (deg/s) and updates the global
// axisG (averaged |monitored axis| in g) used for flat detection. Also the loop's 1-s tick.
float sampleSecond() {
  float peak = 0.0;
  float accSum = 0.0;
  for (int i = 0; i < 20; i++) {           // 20 samples x 50 ms = ~1 s
    mpu.update();
    float gx = mpu.getGyroX();
    float gy = mpu.getGyroY();
    float gz = mpu.getGyroZ();
    float mag = sqrt(gx * gx + gy * gy + gz * gz);
    if (mag > peak) peak = mag;
    accSum += fabs(monitoredAxisAccel());
    delay(50);
  }
  axisG = accSum / 20.0;
  return peak;
}

void setup() {
  Serial.begin(9600);          // for bench calibration only; does NOT block if no monitor
  gsmSetup();
  analogReadResolution(10);    // 0..1023

  Wire.begin();
  mpu.begin();
  delay(1000);
  mpu.calcOffsets(true, false); // gyro offsets only: hold still ~1s, ANY orientation is fine.
                                // (Full calcOffsets() would also re-zero the accelerometer,
                                //  which assumes the board is lying FLAT at power-on and
                                //  would corrupt the lying-on-side detection otherwise.)

  // Boot confirmation SMS (also proves GSM + battery read are working on install)
  float vbat = readBatteryVolts();
  sendSMS((String("Foal alarm online. Batt ") + batteryPercent(vbat) + "% (" + String(vbat, 2) + "V)").c_str());
}

void loop() {
  float   motion = sampleSecond();                 // ~1 s elapsed here; also sets axisG
  boolean flat   = HANGING_MOUNT ? (axisG < SLACK_THRESHOLD_G)
                                 : (axisG > FLAT_THRESHOLD_G);
  boolean moving = (motion > MOTION_THRESHOLD);

  // Calibration / debug output (harmless in the field with no monitor attached)
  Serial.print("accX="); Serial.print(mpu.getAccX(), 2);
  Serial.print(" accY="); Serial.print(mpu.getAccY(), 2);
  Serial.print(" accZ="); Serial.print(mpu.getAccZ(), 2);
  Serial.print(" axisG="); Serial.print(axisG, 2);
  Serial.print(" motion="); Serial.print(motion, 0);
  Serial.print(" flat="); Serial.println(flat);

  // Age out the restlessness window
  if (windowTimer > 0) {
    windowTimer--;
  } else {
    episodeCount  = 0;
    earlyWarnSent = false;
  }

  if (flat) {
    flatCount++;
    if (moving) activeWhileFlat++;

    // Count toward restlessness once flat has held a few seconds - a hanging box
    // can swing through a 1-2 s fake "flat" blip that must not count as an episode
    if (flatCount == 3) {
      episodeCount++;
      windowTimer = EPISODE_WINDOW_SECS;
    }

    // (1) LABOUR: flat AND genuinely moving -> fast, confident alarm
    if (!labourSent && activeWhileFlat >= ACTIVE_SECS_LABOUR) {
      sendSMS("FOALING ALARM - lying down & active");
      blinkOn();
      labourSent = true;
    }

    // (2) BACKSTOP: flat a very long time even if calm (cast mare / missed event)
    if (!labourSent && flatCount >= CALM_FLAT_BACKSTOP) {
      sendSMS("CHECK MARE - lying flat a long time");
      blinkOn();
      labourSent = true;
    }

  } else {
    // She's back up: reset the flat-episode counters
    flatCount       = 0;
    activeWhileFlat = 0;
    labourSent      = false;
  }

  // (3) EARLY WARNING: repeated up/down cycles in the window -> restless
  if (!earlyWarnSent && episodeCount >= RESTLESS_EPISODES) {
    sendSMS("Mare restless - foaling may be starting");
    blinkOn();
    earlyWarnSent = true;
  }

  // ---- Battery + daily heartbeat ----
  float vbat = readBatteryVolts();

  // Low-battery warning: require a sustained low reading so a momentary
  // GSM-transmit voltage sag doesn't trip it.
  if (vbat < LOW_BATT_VOLTS) lowBattStreak++;
  else                       lowBattStreak = 0;

  if (!lowBattWarned && lowBattStreak >= 10) {
    sendSMS((String("LOW BATTERY ") + String(vbat, 2) + "V - charge foal alarm").c_str());
    blinkOn();
    lowBattWarned = true;
  }
  if (vbat > BATT_RECOVER_VOLTS) lowBattWarned = false;   // re-arm after a charge

  // Daily "I'm alive" heartbeat with battery state
  heartbeatTimer++;
  if (heartbeatTimer >= HEARTBEAT_INTERVAL_SECS) {
    heartbeatTimer = 0;
    sendSMS((String("Foal alarm OK. Batt ") + batteryPercent(vbat) + "% (" + String(vbat, 2) + "V)").c_str());
  }
}
