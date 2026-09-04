/*
  ESP8266 Automatic Water Pump Controller + WiFi reporting
  - Reads soil moisture, controls the pump relay (your existing logic)
  - Reports readings to a backend over WiFi (HTTP POST, JSON)
  - Works with a local LAN server (HTTP) or a public server like Render (HTTPS)

  Wiring (NodeMCU / Wemos D1 mini):
    Soil moisture sensor AO -> A0
    Relay IN               -> GPIO14 (D5)
    VCC -> 3V3, GND -> GND
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= CONFIG ================= */
const char* WIFI_SSID     = "A55";
const char* WIFI_PASSWORD = "123456789100";

// --- Backend target ---
// Local server on your LAN:  USE_HTTPS = false, host = 192.168.1.100, port = 5000
// Render (public URL):       USE_HTTPS = true,  host = cdnb-render-build.onrender.com, port = 443
const bool  USE_HTTPS    = true;
const char* BACKEND_HOST = "cdnb-render-build.onrender.com";  // NO "https://", NO trailing "/"
const int   BACKEND_PORT = 443;                      // 443 for HTTPS, 80 for HTTP
const char* BACKEND_PATH = "/api/readings";
const char* DEVICE_ID    = "pump-01";

// Sensor calibration (keep your values)
#define DRY_VALUE   1024   // raw ADC value when sensor is in dry air
#define WET_VALUE   530    // raw ADC value when sensor is fully wet
int MOISTURE_THRESHOLD_PERCENT = 40;

const bool RELAY_ACTIVE_LOW = false;  // true for common active-low relay boards

// Timing
const unsigned long readInterval   = 1000;   // sample every 1 s
const unsigned long uploadInterval = 3000;  // report every 10 s
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

  String payload = "{\"device\":\"" + String(DEVICE_ID) +
                   "\",\"raw\":" + String(raw) +
                   ",\"moisture\":" + String(moisture) +
                   ",\"pump\":" + String(pumpOn ? "true" : "false") +
                   "}";

  HTTPClient http;
  bool began;

  if (USE_HTTPS) {
    static WiFiClientSecure client;
    client.setInsecure();  // skip cert check; pin the fingerprint in production
    began = http.begin(client, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH, true);
  } else {
    static WiFiClient client;
    began = http.begin(client, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH);
  }

  if (!began) return;

  http.setTimeout(15000);  // tolerate Render cold starts (~30-50 s on free tier)
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  int code = http.POST(payload);
  if (code > 0) {
    Serial.print("[HTTP] ");
    Serial.print(code);
    Serial.print(" -> ");
    Serial.println(http.getString());   // → [HTTP] 200 -> {"ok":true}
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
