#include <MKRGSM.h>

#define PINNUMBER "1503"

GSM gsmAccess;
GSM_SMS sms;

// Foal alarm constants
const uint8_t sensorSwitchPIN = 1;
const String payload = "FOALING ALARM!";
const char numberToSMS = "+447535900976‬";
const uint8_t debug = false;
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
//    char remoteNumber[numberToSMS.length() + 1];
//    numberToSMS.toCharArray(remoteNumber, numberToSMS.length() + 1);
    
    char txtMsg[payload.length() + 1];
    payload.toCharArray(txtMsg, payload.length() + 1);
    
    sms.beginSMS(numberToSMS);
    sms.print(txtMsg);
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

   if (Count == 15) {
    sendSMS();
    blinkOn();
   }

   if (Count == 30) {
    sendSMS();
    blinkOn();
   }

   if (Count == 60) {
    sendSMS();
    blinkOn();
   }
}
