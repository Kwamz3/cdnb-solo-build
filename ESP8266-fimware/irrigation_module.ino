/*
  ESP8266 Automatic Water Pump Controller + WiFi reporting + CLOUD CONTROL
  ---------------------------------------------------------------
  - Reads soil moisture, controls the pump relay (existing logic)
  - Reports readings to the backend (POST /api/readings, JSON)
  - PULLS commands from the backend (GET /api/state) so the web
    dashboard's buttons can actually control the pump.

  WHY PULLING? The ESP8266 is behind your home router's NAT; the
  server (Render) can never open a connection TO the device. So the
  device polls the server every uploadInterval and applies the
  "desired state" it finds. The button -> server -> device -> telemetry
  loop is the only direction that works.

  Wiring (NodeMCU / Wemos D1 mini):
    Soil moisture sensor AO -> A0
    Relay IN               -> GPIO14 (D5)
    VCC -> 3V3, GND -> GND

  Libraries: ArduinoJson v6 (Library Manager -> "ArduinoJson by Benoit Blanchon")
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

/* ================= CONFIG ================= */
const char* WIFI_SSID     = "A55";
const char* WIFI_PASSWORD = "123456789100";

// --- Backend target ---
// Local server on your LAN:  USE_HTTPS = false, host = 192.168.1.100, port = 5000
// Render (public URL):       USE_HTTPS = true,  host = your-app.onrender.com, port = 443
const bool  USE_HTTPS    = true;
const char* BACKEND_HOST = "cdnb-render-build.onrender.com";  // no "https://", no trailing "/"
const int   BACKEND_PORT = 443;                      // 443 for HTTPS, 80 for HTTP
const char* READINGS_PATH = "/api/readings";
const char* STATE_PATH    = "/api/state?device=pump-01";   // command channel
const char* DEVICE_ID    = "pump-01";

// Sensor calibration (keep your values)
#define DRY_VALUE   1024   // raw ADC value when sensor is in dry air
#define WET_VALUE   530    // raw ADC value when sensor is fully wet
// NOTE: now a variable, not const: the server can change it via /api/controls
int MOISTURE_THRESHOLD_PERCENT = 40;

// Server-driven settings (defaults = standalone behaviour until first poll)
bool autoMode       = true;   // false  -> obey the web override
bool manualOverride = false;  // from   state.override.active
bool overrideOn     = false;  // from   state.override.on
bool rainExpected   = false;  // from   state.rainExpected (skip watering)
int  waterLevel     = 100;    // from   state.waterLevel (%)
const int  WATER_SAFETY_LEVEL = 10;   // don't run the pump below this
int  lastAppliedSeq = 0;      // last commandSeq we applied (ACKed in telemetry)

const bool RELAY_ACTIVE_LOW = false;  // true for common active-low relay boards

// Timing
const unsigned long readInterval     = 1000;   // sample every 1 s
const unsigned long uploadInterval   = 10000;  // poll + report every 10 s
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

/* ================= RELAY ================= */
void setPump(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(relayPin, turnOn ? HIGH : LOW);
  }
}

/* ================= WIFI ================= */
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
    Serial.println("WiFi connect FAILED (will retry on next cycle)");
  }
}

/* ================= COMMAND CHANNEL (new) ================= */

// GET a path and return the HTTP status code; body goes into `response`.
// Returns a negative ESP8266HTTPClient error code on transport failure.
int httpGet(const String& path, String& response) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return -1;
  }

  HTTPClient http;
  bool began = false;

  if (USE_HTTPS) {
    static WiFiClientSecure client;   // static: reuse the TLS session
    client.setInsecure();             // skip cert check; pin the fingerprint in production
    began = http.begin(client, String(BACKEND_HOST), BACKEND_PORT, path, true);
  } else {
    static WiFiClient client;
    began = http.begin(client, String(BACKEND_HOST), BACKEND_PORT, path);
  }

  if (!began) {
    Serial.println("[HTTP] GET begin failed");
    return -2;
  }

  http.setTimeout(10000);             // GET is small; Render should be warm from polling
  http.addHeader("Connection", "close");

  int code = http.GET();
  if (code > 0) {
    response = http.getString();
  } else {
    Serial.print("[HTTP] GET failed: code "); Serial.print(code);
    Serial.print(" ("); Serial.print(http.errorToString(code)); Serial.println(")");
  }
  http.end();
  return code;
}

// Poll GET /api/state, parse only the fields we care about (JSON filter
// keeps the RAM cost tiny), and apply them to the globals.
// Returns true if anything changed.
bool fetchAndApplyState() {
  String response;
  int code = httpGet(String(STATE_PATH), response);
  if (code != 200) {
    Serial.print("[state] HTTP "); Serial.println(code);
    return false;                       // keep last known settings; retry next cycle
  }

  // Filter: only deserialize the fields the device needs
  StaticJsonDocument<256> filter;
  filter["threshold"]     = true;
  filter["autoMode"]      = true;
  filter["override"]["active"] = true;
  filter["override"]["on"]     = true;
  filter["rainExpected"]  = true;
  filter["waterLevel"]    = true;
  filter["commandSeq"]    = true;

  StaticJsonDocument<384> doc;
  DeserializationError err =
      deserializeJson(doc, response, DeserializationOption::Filter(filter));
  if (err) {
    Serial.print("[state] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  bool changed = false;

  if (!doc["threshold"].isNull()) {
    int t = doc["threshold"].as<int>();
    if (t >= 10 && t <= 90 && t != MOISTURE_THRESHOLD_PERCENT) {
      MOISTURE_THRESHOLD_PERCENT = t;
      changed = true;
    }
  }
  if (!doc["autoMode"].isNull()) {
    bool a = doc["autoMode"].as<bool>();
    if (a != autoMode) { autoMode = a; changed = true; }
  }
  if (!doc["override"]["active"].isNull()) {
    bool a = doc["override"]["active"].as<bool>();
    if (a != manualOverride) { manualOverride = a; changed = true; }
  }
  if (!doc["override"]["on"].isNull()) {
    bool o = doc["override"]["on"].as<bool>();
    if (o != overrideOn) { overrideOn = o; changed = true; }
  }
  if (!doc["rainExpected"].isNull()) {
    bool r = doc["rainExpected"].as<bool>();
    if (r != rainExpected) { rainExpected = r; changed = true; }
  }
  if (!doc["waterLevel"].isNull()) {
    int w = doc["waterLevel"].as<int>();
    if (w != waterLevel) { waterLevel = w; changed = true; }
  }
  if (!doc["commandSeq"].isNull()) {
    int s = doc["commandSeq"].as<int>();
    if (s != lastAppliedSeq) {
      lastAppliedSeq = s;               // ACKed in the next telemetry POST
      changed = true;
    }
  }

  if (changed) {
    Serial.println("[state] applied:");
    Serial.print("  threshold="); Serial.print(MOISTURE_THRESHOLD_PERCENT);
    Serial.print(" autoMode="); Serial.print(autoMode ? "AUTO" : "MANUAL");
    Serial.print(" override="); Serial.print(manualOverride ? (overrideOn ? "ON" : "OFF") : "-");
    Serial.print(" rain="); Serial.print(rainExpected ? "YES" : "no");
    Serial.print(" water="); Serial.print(waterLevel);
    Serial.print(" seq="); Serial.println(lastAppliedSeq);
  }
  return changed;
}

// Decide the pump target from server commands (or fall back to the local
// threshold logic when the server has not sent a manual override).
void updatePump(int moisture) {
  bool target;
  const char* reason;

  if (manualOverride) {
    target = overrideOn;                                   // button wins
    reason = overrideOn ? "manual ON" : "manual OFF";
  } else if (rainExpected) {
    target = false;                                        // rain skip
    reason = "rain expected";
  } else if (waterLevel < WATER_SAFETY_LEVEL) {
    target = false;                                        // protect pump
    reason = "reservoir low";
  } else {
    target = (moisture < MOISTURE_THRESHOLD_PERCENT);      // default auto logic
    reason = target ? "soil dry" : "soil wet";
  }

  setPump(target);
  currentPumpState = target;

  if (target != lastPumpState) {
    Serial.print(currentPumpState ? ">>> PUMP ON (" : ">>> PUMP OFF (");
    Serial.print(reason); Serial.println(") <<<");
    lastPumpState = currentPumpState;
  }
}

/* ================= TELEMETRY (unchanged shape + seq ACK) ================= */
void sendReading(int raw, int moisture, bool pumpOn) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return; // skip this upload, retry later
  }

  String payload = "{\"device\":\"" + String(DEVICE_ID) +
                   "\",\"raw\":" + String(raw) +
                   ",\"moisture\":" + String(moisture) +
                   ",\"pump\":" + String(pumpOn ? "true" : "false") +
                   ",\"seq\":" + String(lastAppliedSeq) +   // command ACK
                   "}";

  for (int attempt = 1; attempt <= 2; attempt++) {
    HTTPClient http;
    bool began = false;

    if (USE_HTTPS) {
      static WiFiClientSecure client;
      client.setInsecure();
      began = http.begin(client, String(BACKEND_HOST), BACKEND_PORT, String(READINGS_PATH), true);
    } else {
      static WiFiClient client;
      began = http.begin(client, String(BACKEND_HOST), BACKEND_PORT, String(READINGS_PATH));
    }

    if (!began) {
      Serial.println("[HTTP] begin failed");
      return;
    }

    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");

    int code = http.POST(payload);
    if (code > 0) {
      Serial.print("[HTTP] "); Serial.print(code);
      Serial.print(" -> "); Serial.println(http.getString());
      http.end();
      return;
    }

    Serial.print("[HTTP] attempt "); Serial.print(attempt);
    Serial.print(" failed: code "); Serial.print(code);
    Serial.print(" ("); Serial.print(http.errorToString(code));
    Serial.println(")");
    http.end();

    if (attempt == 1) delay(2000);
  }
}

/* ================= SETUP ================= */
void setup() {
  pinMode(relayPin, OUTPUT);
  setPump(false);              // pump OFF at boot
  currentPumpState = false;
  lastPumpState    = false;

  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("ESP8266 Pump Controller - GPIO14 READY");
  Serial.println("(cloud control: polling /api/state)");
  Serial.println("========================================");

  connectWiFi();
}

/* ================= LOOP ================= */
void loop() {
  unsigned long now = millis();

  // --- sample sensor (every 1 s) ---
  if (now - lastReadTime >= readInterval) {
    lastReadTime = now;

    int raw = analogRead(sensorPin);
    int moisture = map(raw, DRY_VALUE, WET_VALUE, 0, 100);
    moisture = constrain(moisture, 0, 100);

    lastRaw      = raw;
    lastMoisture = moisture;

    updatePump(moisture);   // <-- now takes server commands into account

    Serial.print("Raw: "); Serial.print(raw);
    Serial.print(" | Moisture: "); Serial.print(moisture);
    Serial.print("% | Pump: "); Serial.println(currentPumpState ? "ON" : "OFF");
  }

  // --- command poll + telemetry report (every 10 s) ---
  if (now - lastUploadTime >= uploadInterval) {
    lastUploadTime = now;
    fetchAndApplyState();                       // 1) pull commands from dashboard
    updatePump(lastMoisture);                   //    re-apply immediately if changed
    sendReading(lastRaw, lastMoisture, currentPumpState);   // 2) report + ACK
  }
}
