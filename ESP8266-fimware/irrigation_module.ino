/*
  ESP8266 Automatic Water Pump Controller + WiFi reporting
  - Reads soil moisture, controls the pump relay (your existing logic)
  - Reports readings to a backend over WiFi (HTTP POST, JSON)

  Wiring (NodeMCU / Wemos D1 mini):
    Soil moisture sensor AO -> A0
    Relay IN               -> GPIO14 (D5)
    VCC -> 3V3, GND -> GND
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

/* ================= CONFIG ================= */
const char* WIFI_SSID     = "A55";
const char* WIFI_PASSWORD = "123456789100";

// Point these at your backend (LAN IP, or a public hostname)
const char* BACKEND_HOST = "192.168.1.100";
const int   BACKEND_PORT = 5000;
const char* BACKEND_PATH = "/api/readings";
const char* DEVICE_ID    = "pump-01";

// Sensor calibration (keep your values)
#define DRY_VALUE   1024   // raw ADC value when sensor is in dry air
#define WET_VALUE   530    // raw ADC value when sensor is fully wet
int MOISTURE_THRESHOLD_PERCENT = 40;

const bool RELAY_ACTIVE_LOW = false;  // true for common active-low relay boards

// Timing
const unsigned long readInterval   = 1000;   // sample every 1 s
const unsigned long uploadInterval = 10000;  // report every 10 s
unsigned long lastReadTime   = 0;
unsigned long lastUploadTime = 0;

/* ================= PINS ================= */
const int sensorPin = A0;   // analog input
const int relayPin  = 14;   // GPIO14 = D5 on NodeMCU

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

  Serial.print("Connecting to WiFi ");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connect FAILED (will retry on next upload)");
  }
}

void sendReading(int raw, int moisture, bool pumpOn) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return; // skip this upload, retry later
  }

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH)) {
    Serial.println("[HTTP] begin failed");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  // Build JSON manually (no extra library needed)
  String payload = "{\"device\":\"" + String(DEVICE_ID) +
                   "\",\"raw\":" + String(raw) +
                   ",\"moisture\":" + String(moisture) +
                   ",\"pump\":" + String(pumpOn ? "true" : "false") +
                   "}";

  int code = http.POST(payload);
  if (code > 0) {
    Serial.print("[HTTP] "); Serial.print(code);
    Serial.print(" -> "); Serial.println(http.getString());
  } else {
    Serial.print("[HTTP] POST failed: ");
    Serial.println(http.errorToString(code));
  }
  http.end();
}

void setup() {
  pinMode(relayPin, OUTPUT);
  setPump(false);              // pump OFF at boot
  currentPumpState = false;
  lastPumpState    = false;

  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("ESP8266 Pump Controller - GPIO14 READY");
  Serial.println("========================================");

  connectWiFi();
}

void loop() {
  unsigned long now = millis();

  // --- sample sensor & run irrigation logic (unchanged) ---
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
      Serial.println(currentPumpState ? ">>> PUMP ON <<<" : ">>> PUMP OFF <<<");
      lastPumpState = currentPumpState;
    }
  }

  // --- report to backend (non-blocking, every uploadInterval) ---
  if (now - lastUploadTime >= uploadInterval) {
    lastUploadTime = now;
    sendReading(lastRaw, lastMoisture, currentPumpState);
  }
}
