/*
  ESP8266 Automatic Water Pump Controller - FIXED
*/

#include <Esp.h>

const int sensorPin   = A0;   // Water sensor analog output -> A0
const int relayPin    = 14;   // GPIO14 (D5 on NodeMCU)

#define DRY_VALUE   1024   
#define WET_VALUE   530    

int MOISTURE_THRESHOLD_PERCENT = 40;
const bool RELAY_ACTIVE_LOW = false; // Set to true for standard active-low relay modules

unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000; 

bool currentPumpState = false;  
bool lastPumpState = false;

void setPump(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(relayPin, turnOn ? HIGH : LOW);
  }
}

void setup() {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, HIGH); 
  } else {
    digitalWrite(relayPin, LOW);
  }
  pinMode(relayPin, OUTPUT);
  
  Serial.begin(115200);
  delay(1000);
  
  setPump(false);
  currentPumpState = false;
  lastPumpState = false;
  
  Serial.println("========================================");
  Serial.println("ESP8266 Pump Controller - GPIO14 READY");
  Serial.println("========================================");
}

void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastReadTime >= readInterval) {
    lastReadTime = currentTime;

    int sensorValue = analogRead(sensorPin);
    int moisturePercent = map(sensorValue, DRY_VALUE, WET_VALUE, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);

    // Irrigation logic: Pump turns ON when moisture drops below threshold (Dry)
    if (moisturePercent < MOISTURE_THRESHOLD_PERCENT) {
      setPump(true);
      currentPumpState = true;
    } else {
      setPump(false);
      currentPumpState = false;
    }

    Serial.print("Raw: "); Serial.print(sensorValue);
    Serial.print(" | Moisture: "); Serial.print(moisturePercent);
    Serial.print("% | Pump: "); Serial.println(currentPumpState ? "ON" : "OFF");

    if (currentPumpState != lastPumpState) {
      Serial.println(currentPumpState ? ">>> PUMP ON <<<" : ">>> PUMP OFF <<<");
      lastPumpState = currentPumpState;
    }
  }
}