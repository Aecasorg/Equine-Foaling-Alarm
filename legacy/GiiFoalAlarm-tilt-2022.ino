// LEGACY firmware - the version deployed on the horse from ~2022 until the 2026
// MPU-6050 rewrite. Two ball tilt switches wired in parallel on pin 1 (either
// closing = mare lying flat), single 30-second threshold (the original 2020
// firmware's 15/60 s tiers commented out). Kept for reference only; it lives in
// this subfolder so the Arduino IDE never tries to compile it alongside the
// current sketch. SIM PIN and phone number redacted (see arduino_secrets.h
// pattern used by the current sketch).

#include <MKRGSM.h>

#define PINNUMBER "REDACTED"

GSM gsmAccess;
GSM_SMS sms;

// Foal alarm constants
const uint8_t sensorSwitchPIN = 1;
char payload[20] = "FOALING ALARM!";
char numberToSMS[20] = "REDACTED";
int Count = 0; // Timer
boolean notConnected = true;

void gsmSetup() {
  while(notConnected) {
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
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
}

void sendSMS() {
    sms.beginSMS(numberToSMS);
    sms.print(payload);
    sms.endSMS();
}

void setup() {
  gsmSetup();

  pinMode(sensorSwitchPIN, INPUT);
}

void loop() {

  delay(1000);

  if (digitalRead(sensorSwitchPIN) == LOW) {
     Count++;
   } else {
     Count = 0;
   }

//   if (Count == 15) {
//    sendSMS();
//    blinkOn();
//   }

   if (Count == 30) {
    sendSMS();
    blinkOn();
   }

//   if (Count == 60) {
//    sendSMS();
//    blinkOn();
//   }
}
