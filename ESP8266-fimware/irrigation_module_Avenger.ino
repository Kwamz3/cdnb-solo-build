/*
  ESP8266 Automatic Water Pump Controller + WiFi reporting
  - Reads soil moisture, controls the pump relay with immediate response
  - Reports readings to a backend over WiFi (HTTP POST, JSON)
  - Works with a local LAN server (HTTP) or a public server like Render (HTTPS)
  
  OPTIMIZED FOR SPEED AND RELIABILITY
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= CONFIG ================= */
const char* WIFI_SSID     = "AV9NG6R";
const char* WIFI_PASSWORD = "123456789100";

// --- Backend target ---
const bool  USE_HTTPS    = true;
const char* BACKEND_HOST = "cdnb-render-build.onrender.com";
const int   BACKEND_PORT = 443;
const char* BACKEND_PATH = "/api/readings";
const char* DEVICE_ID    = "pump-01";

// Sensor calibration
#define DRY_VALUE   1024
#define WET_VALUE   530
const int MOISTURE_THRESHOLD_PERCENT = 40;

const bool RELAY_ACTIVE_LOW = false;

// Optimized timing - MUCH faster response
const unsigned long readInterval   = 100;    // Read every 100ms for faster response
const unsigned long uploadInterval = 2000;   // Report every 2s

/* ================= PINS ================= */
const int sensorPin = A0;
const int relayPin  = D6;

/* ================= STATE ================= */
bool currentPumpState = false;
bool lastPumpState    = false;
int  lastRaw          = 0;
int  lastMoisture     = 0;
bool wifiConnected    = false;
bool uploadInProgress = false;

// Timing variables
unsigned long lastReadTime = 0;
unsigned long lastUploadTime = 0;

// Pre-allocated buffers for speed
char jsonBuffer[128];
char urlBuffer[128];

// Persistent HTTP client objects to avoid re-creation
HTTPClient http;
WiFiClientSecure secureClient;
WiFiClient plainClient;

void setPump(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(relayPin, turnOn ? HIGH : LOW);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return;
  }

  Serial.print("Connecting to WiFi ");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(100);
    Serial.print(".");
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    Serial.println();
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connect FAILED");
  }
}

void sendReading(int raw, int moisture, bool pumpOn) {
  // Don't start new upload if previous is still in progress
  if (uploadInProgress) return;
  
  // Check WiFi connection
  if (!wifiConnected) {
    connectWiFi();
    if (!wifiConnected) return;
  }

  // Build JSON quickly using sprintf (faster than String concatenation)
  snprintf(jsonBuffer, sizeof(jsonBuffer), 
    "{\"device\":\"%s\",\"raw\":%d,\"moisture\":%d,\"pump\":%s}",
    DEVICE_ID, raw, moisture, pumpOn ? "true" : "false"
  );

  uploadInProgress = true;
  int code = -1;

  // Setup HTTP connection
  bool began = false;
  if (USE_HTTPS) {
    secureClient.setInsecure();
    began = http.begin(secureClient, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH, true);
  } else {
    began = http.begin(plainClient, BACKEND_HOST, BACKEND_PORT, BACKEND_PATH);
  }

  if (began) {
    http.setTimeout(5000);  // Shorter timeout for faster response
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    
    code = http.POST(jsonBuffer);
    
    if (code > 0) {
      String response = http.getString();
      
      // Parse response for pump commands
      if (response.indexOf("\"pumpStatus\":true") >= 0) {
        if (!currentPumpState) {
          setPump(true);
          currentPumpState = true;
          lastPumpState = true;
        }
      } else if (response.indexOf("\"pumpStatus\":false") >= 0) {
        if (currentPumpState) {
          setPump(false);
          currentPumpState = false;
          lastPumpState = false;
        }
      }
    }
    http.end();
  }

  uploadInProgress = false;
}

void setup() {
  pinMode(relayPin, OUTPUT);
  setPump(false);
  currentPumpState = false;
  lastPumpState = false;

  Serial.begin(115200);
  delay(100);

  Serial.println("========================================");
  Serial.println("ESP8266 Pump Controller - OPTIMIZED");
  Serial.println("========================================");

  // Initialize WiFi
  connectWiFi();
  
  // Initialize HTTP client once
  http.setReuse(true);
}

void loop() {
  unsigned long now = millis();

  // --- READ SENSOR AND CONTROL PUMP (HIGH PRIORITY) ---
  if (now - lastReadTime >= readInterval) {
    lastReadTime = now;

    // Read sensor
    int raw = analogRead(sensorPin);
    int moisture = map(raw, DRY_VALUE, WET_VALUE, 0, 100);
    moisture = constrain(moisture, 0, 100);

    lastRaw = raw;
    lastMoisture = moisture;

    // IMMEDIATE PUMP CONTROL - no delays
    bool shouldPumpOn = (moisture < MOISTURE_THRESHOLD_PERCENT);
    
    if (shouldPumpOn != currentPumpState) {
      setPump(shouldPumpOn);
      currentPumpState = shouldPumpOn;
      
      // Log state changes
      Serial.print("Raw: "); Serial.print(raw);
      Serial.print(" | Moisture: "); Serial.print(moisture);
      Serial.print("% | Pump: "); Serial.println(currentPumpState ? "ON" : "OFF");
      Serial.println(currentPumpState ? ">>> PUMP ON <<<" : ">>> PUMP OFF <<<");
      
      // Force immediate upload on state change
      lastUploadTime = 0;
    }
  }

  // --- REPORT TO BACKEND (NON-BLOCKING) ---
  if (!uploadInProgress && (now - lastUploadTime >= uploadInterval)) {
    lastUploadTime = now;
    sendReading(lastRaw, lastMoisture, currentPumpState);
  }

  // Small yield to prevent watchdog issues
  yield();
}