/*
  ESP8266 Automatic Water Pump Controller - OPTIMIZED
*/

#include <Esp.h>

const int sensorPin   = A0;   // Water sensor analog output -> A0
const int relayPin    = D6;   // Moved to D5 (GPIO14) to avoid D4/D1 boot issues

#define DRY_VALUE   1024   
#define WET_VALUE   530    

int MOISTURE_THRESHOLD_PERCENT = 40;
const bool RELAY_ACTIVE_LOW = false; 

unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000; 

bool currentPumpState = false;  
bool lastPumpState = false;

void setPump(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    if (turnOn) {
      digitalWrite(relayPin, LOW);  // Send 0V to turn ON active-low relay
    } else {
      digitalWrite(relayPin, HIGH); // Send 3.3V to turn OFF active-low relay
    }
  } else {
    if (turnOn) {
      digitalWrite(relayPin, HIGH); // Send 3.3V to turn ON active-high relay
    } else {
      digitalWrite(relayPin, LOW);  // Send 0V to turn OFF active-high relay
    }
  }
}


void setup() {
  // CRITICAL FIX: Write HIGH to the internal output latch BEFORE setting pinMode.
  // This minimizes active-low relay clicking during initial microsecond boot stages.
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, HIGH); 
  } else {
    digitalWrite(relayPin, LOW);
  }
  pinMode(relayPin, OUTPUT);
  
  Serial.begin(115200);
  delay(1000); // 1 second is enough now that pins are stable
  
  setPump(false);
  currentPumpState = false;
  lastPumpState = false;
  
  Serial.println("========================================");
  Serial.println("ESP8266 Pump Controller - D5 STABLE");
  Serial.println("========================================");
}

void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastReadTime >= readInterval) {
    lastReadTime = currentTime;

    int sensorValue = analogRead(sensorPin);
    int moisturePercent = map(sensorValue, DRY_VALUE, WET_VALUE, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);

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