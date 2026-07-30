// SMS inbox viewer - prints every SMS this SIM receives to the Serial Monitor.
// Use case: one-time verification codes sent TO the foal alarm's SIM
// (e.g. adding the Lebara SIM to the Lebara app), balance texts, etc.
//
// Usage: upload, open Serial Monitor @9600, then request the code in the app.
// Each arriving SMS is printed (sender + body) and then deleted from the modem.

#include <MKRGSM.h>
#include "arduino_secrets.h"   // needs SECRET_PINNUMBER ("" for a PIN-free SIM)

GSM gsmAccess;
GSM_SMS sms;

void setup() {
  Serial.begin(9600);
  while (!Serial);             // wait for the monitor so nothing is missed
  Serial.println("Connecting to network...");
  while (gsmAccess.begin(SECRET_PINNUMBER) != GSM_READY) {
    Serial.println("  not connected yet - retrying");
    delay(1000);
  }
  Serial.println("Connected. Waiting for SMS...");
  Serial.println("(request the verification code in the Lebara app now)");
}

void loop() {
  if (sms.available()) {
    char from[20];
    sms.remoteNumber(from, 20);
    Serial.print("\n--- SMS from ");
    Serial.print(from);          // shortcodes/alphanumeric senders may look odd; the body is what matters
    Serial.println(" ---");
    int c;
    while ((c = sms.read()) != -1) {
      Serial.print((char)c);
    }
    Serial.println("\n--- end ---");
    sms.flush();                 // delete from modem storage so the next one shows cleanly
  }
  delay(500);
}
