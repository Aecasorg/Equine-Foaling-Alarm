#include <MKRGSM.h>
#include <Wire.h>
#include <MPU6050_light.h>    // Library Manager: "MPU6050_light" by rfetick
#include "arduino_secrets.h"  // SIM PIN + alert number - copy arduino_secrets.h.example

#define PINNUMBER SECRET_PINNUMBER

GSM gsmAccess;
GSM_SMS sms;
GSMScanner scannerNetworks;   // AT+CSQ readout for the status texts
MPU6050 mpu(Wire);

// ---- Hardware ----
const uint8_t BATTERY_PIN = A1;   // battery via 2:1 resistor divider (100k / 100k)
const uint8_t PMIC_I2C_ADDR = 0x6B; // BQ24195 charger chip - shares the I2C bus with the MPU
// Every alert goes to each listed number in turn (a few seconds apart).
// Set a slot to "" in arduino_secrets.h to disable it.
const char* smsRecipients[] = { SECRET_PHONE_NUMBER, SECRET_PHONE_NUMBER_2 };
const int   NUM_RECIPIENTS  = sizeof(smsRecipients) / sizeof(smsRecipients[0]);
// (Tilt switches removed. The GY-521's unused pins sit on unused MKR digital
//  strips - treat those digital pins as reserved in any future code.)

// ===== SELF-ARMING FLAT DETECTION - no manual calibration =====
// The box learns its own "upright" reference each time it is fitted: at power-on
// and again after every unplug from the charger it enters a settling state, and
// once the gravity direction has held steady for ARM_SECS it locks the reference
// and texts ARMED. "Lying flat" is then a tilt of more than FLAT_ANGLE_DEG away
// from that reference. Designed for a DANGLING box (self-plumbing, so grazing and
// head position never move the reference); a rigid flat mount also works but with
// less margin against grazing. Fit to a STANDING horse.
const float ARM_CONSISTENT_DEG = 20.0;   // sample within this of the candidate = steady
const int   ARM_SECS           = 120;    // steady this long -> lock reference & arm
const float FLAT_ANGLE_DEG     = 60.0;   // tilt beyond this from reference = flat
const long  ARM_REMINDER_SECS  = 1800;   // unarmed 30 min -> one "check it" nudge SMS

// ---- Trigger thresholds (seconds / deg-per-second) ----
const float MOTION_THRESHOLD    = 30.0; // gyro deg/s that counts as "real movement" (rest noise ~1-2)
const int   ACTIVE_SECS_LABOUR  = 8;    // seconds of movement WHILE FLAT => labour, alarm fast
const int   CALM_FLAT_BACKSTOP  = 0;    // seconds flat-and-calm before a "check mare" nudge.
                                        // 0 = DISABLED (owner's call, 2 Aug 2026: mares rest flat
                                        // routinely, so this alerted on normal sleep). Set e.g.
                                        // 3600 (60 min) to re-enable as a cast-mare safety net.
const int   RESTLESS_EPISODES   = 3;    // up/down cycles within the window => early warning
const int   EPISODE_WINDOW_SECS = 1200; // 20 min window for counting restlessness

// ---- Battery / heartbeat ----
const float DIVIDER_RATIO           = 2.0;     // (R1+R2)/R2; 2.0 for two equal resistors
const float LOW_BATT_VOLTS          = 3.40;    // warn below this
const float BATT_RECOVER_VOLTS      = 3.70;    // re-arm the warning once charged past this
const long  HEARTBEAT_INTERVAL_SECS = 86400L;  // ~24 h (loop ticks ~1 s)
const long  SMS_POLL_SECS           = 30;      // check the inbox for "status" texts every 30 s

// ---- State ----
boolean notConnected  = true;

boolean armed         = false;  // reference locked & monitoring?
float   refX, refY, refZ;       // locked "upright" gravity direction (unit vector)
float   candX, candY, candZ;    // candidate reference while settling (unit vector)
boolean haveCand      = false;
int     stableSecs    = 0;      // how long the candidate has held steady
long    armingSecs    = 0;      // total unarmed time (for the reminder)
boolean armReminded   = false;

float   avgX, avgY, avgZ;       // averaged accel vector over the last sampled second (g)

int  flatCount        = 0;      // seconds continuously flat this episode
int  activeWhileFlat  = 0;      // seconds of movement seen within this flat episode
boolean labourSent    = false;  // one lying-down alarm per flat episode
int  episodeCount     = 0;      // recent up/down cycles
int  windowTimer      = 0;      // counts the restlessness window down
boolean earlyWarnSent = false;

long    heartbeatTimer = 0;     // seconds since last heartbeat
int     lowBattStreak  = 0;     // consecutive low readings (rides out GSM-TX voltage sag)
boolean lowBattWarned  = false;
boolean wasOnCharger   = false; // edge-detect for charger connect/disconnect
boolean fullAnnounced  = false; // one "battery full" SMS per charge session
long    smsPollTimer   = 0;     // counts up to the next inbox check

void gsmSetup() {
  int attempt = 0;
  while (notConnected) {
    if (gsmAccess.begin(PINNUMBER) == GSM_READY) {
      notConnected = false;
      blinkSignal();
    } else {
      attempt++;
      Serial.print("GSM connect failed, retrying (attempt ");
      Serial.print(attempt);
      Serial.println(")");
      digitalWrite(LED_BUILTIN, HIGH);   // one short flash per failed attempt:
      delay(80);                         // "retrying" now looks different from "dead"
      digitalWrite(LED_BUILTIN, LOW);
      delay(920);
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
  for (int i = 0; i < NUM_RECIPIENTS; i++) {
    if (strlen(smsRecipients[i]) < 3) continue;   // "" slot = unused
    sms.beginSMS(smsRecipients[i]);
    sms.print(msg);
    sms.endSMS();
  }
}

// Signal quality 0-31 (99/blank = unknown). SMS usually works from ~5; 10+ is comfortable.
String signalStr() {
  String s = scannerNetworks.getSignalStrength();
  s.trim();
  if (s.length() == 0 || s == "99") return "?/31";
  return s + "/31";
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

// Read the BQ24195's system-status register (0x08). Read-only: no charger
// configuration is ever written, so charge behaviour stays at the board defaults.
// Any bus error reports as "no charger", which leaves field behaviour untouched.
uint8_t pmicStatus() {
  Wire.beginTransmission(PMIC_I2C_ADDR);
  Wire.write(0x08);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(PMIC_I2C_ADDR, (uint8_t)1);
  if (Wire.available() == 0) return 0;
  return Wire.read();
}
boolean onCharger()  { return (pmicStatus() & 0x04) != 0; }    // PG_STAT: external 5V present
boolean chargeDone() { return (pmicStatus() & 0x30) == 0x30; } // CHRG_STAT 11 = charge complete

// Reply to a texted "status" (case-insensitive) with a status report, sent ONLY
// to the number that asked - not the alert list. Every other inbound SMS is read
// and discarded so the modem's message store can't fill up over a season of
// network/marketing texts.
void checkInbox() {
  while (sms.available()) {
    char from[20];
    sms.remoteNumber(from, 20);
    String body = "";
    int c;
    while ((c = sms.read()) != -1) body += (char)c;
    sms.flush();                       // delete from modem storage either way
    body.trim();
    body.toLowerCase();
    if (body.startsWith("status")) {
      float vbat = readBatteryVolts();
      String state = onCharger() ? (chargeDone() ? "on charger, battery full" : "charging")
                                 : (armed ? "armed" : "arming");
      sms.beginSMS(from);
      sms.print((String("Foal alarm status: ") + state + ". Batt " + batteryPercent(vbat)
                 + "% (" + String(vbat, 2) + "V). Sig " + signalStr()).c_str());
      sms.endSMS();
    }
  }
}

// Sample the IMU for ~1 second. Returns PEAK movement (deg/s) and updates the
// global avgX/Y/Z gravity vector. Also provides the loop's 1-second tick.
float sampleSecond() {
  float peak = 0.0;
  float sx = 0.0, sy = 0.0, sz = 0.0;
  for (int i = 0; i < 20; i++) {           // 20 samples x 50 ms = ~1 s
    mpu.update();
    float gx = mpu.getGyroX();
    float gy = mpu.getGyroY();
    float gz = mpu.getGyroZ();
    float mag = sqrt(gx * gx + gy * gy + gz * gz);
    if (mag > peak) peak = mag;
    sx += mpu.getAccX();
    sy += mpu.getAccY();
    sz += mpu.getAccZ();
    delay(50);
  }
  avgX = sx / 20.0;
  avgY = sy / 20.0;
  avgZ = sz / 20.0;
  return peak;
}

float vecMag() { return sqrt(avgX * avgX + avgY * avgY + avgZ * avgZ); }

// Angle (deg) between the current 1-s gravity vector and a stored unit vector.
// Returns -1 if the current vector is unusable (being shaken / far from 1 g).
float angleFrom(float ux, float uy, float uz) {
  float m = vecMag();
  if (m < 0.7 || m > 1.3) return -1.0;
  float d = (avgX * ux + avgY * uy + avgZ * uz) / m;
  if (d > 1.0)  d = 1.0;
  if (d < -1.0) d = -1.0;
  return acos(d) * 57.2958;
}

// Forget the reference and settle afresh (power-on, and after every unplug).
void disarm() {
  armed = false;
  haveCand = false;
  stableSecs = 0;
  armingSecs = 0;
  armReminded = false;
  flatCount = 0;
  activeWhileFlat = 0;
  labourSent = false;
  episodeCount = 0;
  windowTimer = 0;
  earlyWarnSent = false;
}

void setup() {
  Serial.begin(9600);          // bench debug only; does NOT block if no monitor
  pinMode(LED_BUILTIN, OUTPUT);
  gsmSetup();
  analogReadResolution(10);    // 0..1023

  Wire.begin();
  mpu.begin();
  delay(1000);
  mpu.calcOffsets(true, false); // gyro offsets only: hold still ~1s, ANY orientation is fine.
                                // (Full calcOffsets() would also re-zero the accelerometer,
                                //  which assumes the board is lying FLAT at power-on and
                                //  would corrupt the tilt detection otherwise.)

  // Boot confirmation SMS (also proves GSM + battery read are working)
  float vbat = readBatteryVolts();
  sendSMS((String("Foal alarm online. Batt ") + batteryPercent(vbat) + "% (" + String(vbat, 2) + "V). Sig " + signalStr() + ". Arming once fitted & settled").c_str());
}

void loop() {
  float   motion = sampleSecond();                 // ~1 s elapsed here; also sets avgX/Y/Z
  boolean moving = (motion > MOTION_THRESHOLD);

  // ---- Charger handling ----
  // On the charger the box is off the horse - stay dormant. Unplugging starts a
  // fresh settling cycle, so every charge automatically re-fits the reference.
  boolean charging = onCharger();
  if (charging && !wasOnCharger) {
    float v = readBatteryVolts();
    if (v < 0.5) {
      // ~0V on the divider = the on/off switch has the battery out of circuit
      // (we tap the JST side of the switch). Charging like this fills nothing.
      sendSMS("Charger connected but no battery detected (switch OFF?) - NOT charging");
      fullAnnounced = true;   // also suppresses a bogus "battery full" with no battery
    } else {
      sendSMS((String("Charger connected. Batt ") + batteryPercent(v) + "%").c_str());
      fullAnnounced = false;
    }
  }
  if (!charging && wasOnCharger) {
    disarm();
  }
  wasOnCharger = charging;

  if (charging) {
    if (!fullAnnounced && chargeDone()) {
      sendSMS("Battery full - foal alarm ready");
      fullAnnounced = true;
    }

  } else if (!armed) {
    // ---- Settling: learn the fitted orientation, then arm ----
    armingSecs++;
    float m = vecMag();
    if (m > 0.7 && m < 1.3) {
      if (!haveCand) {
        candX = avgX / m; candY = avgY / m; candZ = avgZ / m;
        haveCand = true;
        stableSecs = 0;
      } else {
        float a = angleFrom(candX, candY, candZ);
        if (a >= 0 && a < ARM_CONSISTENT_DEG) {
          // steady - drift the candidate gently toward the measured direction
          candX = candX * 0.95 + (avgX / m) * 0.05;
          candY = candY * 0.95 + (avgY / m) * 0.05;
          candZ = candZ * 0.95 + (avgZ / m) * 0.05;
          float cm = sqrt(candX * candX + candY * candY + candZ * candZ);
          candX /= cm; candY /= cm; candZ /= cm;
          stableSecs++;
        } else {
          // orientation changed (being handled/fitted) - start the clock again
          candX = avgX / m; candY = avgY / m; candZ = avgZ / m;
          stableSecs = 0;
        }
      }
      if (stableSecs >= ARM_SECS) {
        refX = candX; refY = candY; refZ = candZ;
        armed = true;
        sendSMS((String("Foal alarm ARMED. Batt ") + batteryPercent(readBatteryVolts()) + "%. Sig " + signalStr()).c_str());
        blinkOn();
      }
    } else {
      stableSecs = 0;   // being shaken/carried - keep waiting
    }
    if (!armReminded && armingSecs >= ARM_REMINDER_SECS) {
      sendSMS("Foal alarm NOT armed yet - check it is clipped on and hanging still");
      armReminded = true;
    }

  } else {
    // ---- ARMED monitoring ----
    float   tilt = angleFrom(refX, refY, refZ);
    boolean flat = (tilt >= 0 && tilt > FLAT_ANGLE_DEG);

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

      // Count toward restlessness once flat has held a few seconds - a swinging
      // box can fake a 1-2 s "flat" blip that must not count as an episode
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

      // (2) BACKSTOP (disabled while CALM_FLAT_BACKSTOP == 0): flat a very long
      // time even if calm - the quiet-labour / cast-mare net
      if (!labourSent && CALM_FLAT_BACKSTOP > 0 && flatCount >= CALM_FLAT_BACKSTOP) {
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
  }

  // Bench debug (harmless in the field with no monitor attached)
  Serial.print("state=");
  Serial.print(charging ? "charging" : (armed ? "armed" : "arming"));
  Serial.print(" |a|="); Serial.print(vecMag(), 2);
  Serial.print(" tilt=");
  if (armed)         Serial.print(angleFrom(refX, refY, refZ), 0);
  else if (haveCand) Serial.print(angleFrom(candX, candY, candZ), 0);
  else               Serial.print(-1);
  Serial.print(" stable="); Serial.print(stableSecs);
  Serial.print(" motion="); Serial.println(motion, 0);

  // ---- Battery + daily heartbeat ----
  float vbat = readBatteryVolts();

  // Low-battery warning: require a sustained low reading so a momentary
  // GSM-transmit voltage sag doesn't trip it.
  // (< 0.5V means the switch has the battery out of circuit - that's a
  //  switched-off state, not a flat battery, so it must not trip the warning)
  if (vbat > 0.5 && vbat < LOW_BATT_VOLTS) lowBattStreak++;
  else                                     lowBattStreak = 0;

  if (!lowBattWarned && lowBattStreak >= 10) {
    sendSMS((String("LOW BATTERY ") + String(vbat, 2) + "V - charge foal alarm").c_str());
    blinkOn();
    lowBattWarned = true;
  }
  if (vbat > BATT_RECOVER_VOLTS) lowBattWarned = false;   // re-arm after a charge

  // Daily "I'm alive" heartbeat with battery + armed state
  heartbeatTimer++;
  if (heartbeatTimer >= HEARTBEAT_INTERVAL_SECS) {
    heartbeatTimer = 0;
    sendSMS((String("Foal alarm OK (") + (armed ? "armed" : "NOT armed") + "). Batt " + batteryPercent(vbat) + "% (" + String(vbat, 2) + "V). Sig " + signalStr()).c_str());
  }

  // ---- Inbound SMS: answer "status" queries (reply only to the sender) ----
  smsPollTimer++;
  if (smsPollTimer >= SMS_POLL_SECS) {
    smsPollTimer = 0;
    checkInbox();
  }
}
