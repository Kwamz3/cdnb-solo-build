/*
  ESP8266 Automatic Water Pump Controller + WiFi
  FIXED: Restored D6 Pin, Active-Low Relay Logic, Non-blocking WiFi
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= CONFIG ================= */
const char* WIFI_SSID     = "A55";
const char* WIFI_PASSWORD = "123456789100";

// Backend Target
const bool  USE_HTTPS    = true;
const char* BACKEND_HOST = "cdnb-render-build.onrender.com";
const int   BACKEND_PORT = 443;
const char* BACKEND_PATH = "/api/readings";
const char* DEVICE_ID    = "pump-01";

// Sensor Calibration
#define DRY_VALUE   1024   
#define WET_VALUE   530    
int MOISTURE_THRESHOLD_PERCENT = 40;

// Set to true because most relay modules trigger on LOW (0V)
const bool RELAY_ACTIVE_LOW = true;  

// Timing Configuration
const unsigned long readInterval   = 1000;   // Check sensor every 1 sec
const unsigned long uploadInterval = 5000;   // Upload to server every 5 sec
unsigned long lastReadTime   = 0;
unsigned long lastUploadTime = 0;

/* ================= PINS ================= */
const int sensorPin = A0;   
const int relayPin  = D6;   // Restored back to D6 (GPIO 12) from working code

/* ================= STATE ================= */
bool currentPumpState = false;
bool lastPumpState    = false;
int  lastRaw          = 0;
int  lastMoisture     = 0;

void setPump(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(relayPin, turnOn ? HIGH : LOW);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void sendReading(int raw, int moisture, bool pumpOn) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return; // Don't block hardware if WiFi is connecting
  }

  String payload = "{\"device\":\"" + String(DEVICE_ID) +
                   "\",\"raw\":" + String(raw) +
                   ",\"moisture\":" + String(moisture) +
                   ",\"pump\":" + String(pumpOn ? "true" : "false") +
                   "}";

  HTTPClient http;
  bool began = false;

  if (USE_HTTPS) {
    static WiFiClientSecure client;
    client.setInsecure();  
    began = http.begin(client, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH, true);
  } else {
    static WiFiClient client;
    began = http.begin(client, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH);
  }

  if (!began) return;

  http.setTimeout(2000);  // Short 2s timeout prevents watchdog crash
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  int code = http.POST(payload);
  if (code > 0) {
    Serial.print("[HTTP Success] Code: ");
    Serial.println(code);
  } else {
    Serial.print("[HTTP Failed] Code: ");
    Serial.println(code);
  }
  http.end();
}

void setup() {
  // Safe initial pre-latch state
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, HIGH); 
  } else {
    digitalWrite(relayPin, LOW);
  }
  pinMode(relayPin, OUTPUT);
  
  setPump(false);
  currentPumpState = false;
  lastPumpState    = false;

  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("ESP8266 Pump Controller - D6 RESTORED");
  Serial.println("========================================");

  connectWiFi();
}

void loop() {
  unsigned long now = millis();

  // --- 1. SENSOR SAMPLING & RELAY CONTROL (High Priority) ---
  if (now - lastReadTime >= readInterval) {
    lastReadTime = now;

    int raw = analogRead(sensorPin);
    int moisture = map(raw, DRY_VALUE, WET_VALUE, 0, 100);
    moisture = constrain(moisture, 0, 100);

    lastRaw      = raw;
    lastMoisture = moisture;

    if (moisture < MOISTURE_THRESHOLD_PERCENT) {
      setPump(true);
      currentPumpState = true;
    } else {
      setPump(false);
      currentPumpState = false;
    }

    Serial.print("Raw: "); Serial.print(raw);
    Serial.print(" | Moisture: "); Serial.print(moisture);
    Serial.print("% | Pump: "); Serial.println(currentPumpState ? "ON" : "OFF");

    if (currentPumpState != lastPumpState) {
      Serial.println(currentPumpState ? ">>> RELAY TRIGGERED: PUMP ON <<<" : ">>> RELAY TRIGGERED: PUMP OFF <<<");
      lastPumpState = currentPumpState;
    }
  }

  // --- 2. WIRELESS BACKEND REPORTING (Low Priority) ---
  if (now - lastUploadTime >= uploadInterval) {
    lastUploadTime = now;
    sendReading(lastRaw, lastMoisture, currentPumpState);
  }
  
  yield(); // Keeps Watchdog Timer happy
}