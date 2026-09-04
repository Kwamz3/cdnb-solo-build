/*
  ESP8266 Automatic Water Pump Controller + High-Speed MQTT
  - Publishes soil moisture telemetry via MQTT (< 50ms)
  - Subscribes to immediate pump commands from dashboard/backend
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/* ================= CONFIG ================= */
const char* WIFI_SSID     = "A55";
const char* WIFI_PASSWORD = "123456789100";

// MQTT Broker (Match the host in your Flask backend)
const char* MQTT_BROKER   = "broker.hivemq.com";
const int   MQTT_PORT     = 1883;

// Topics
const char* TOPIC_TELEMETRY = "irrigation/pump-01/telemetry";
const char* TOPIC_CONTROL   = "irrigation/pump-01/control";
const char* DEVICE_ID       = "pump-01";

// Sensor Calibration
#define DRY_VALUE   1024
#define WET_VALUE   530
int MOISTURE_THRESHOLD_PERCENT = 40;

const bool RELAY_ACTIVE_LOW = false;

// Timing
const unsigned long readInterval   = 1000;   // Sample sensor every 1s
const unsigned long uploadInterval = 3000;   // Publish over MQTT every 3s (can be even faster with MQTT)
unsigned long lastReadTime   = 0;
unsigned long lastUploadTime = 0;

/* ================= PINS ================= */
const int sensorPin = A0;
const int relayPin  = D6;

/* ================= STATE & CLIENTS ================= */
bool currentPumpState = false;
bool lastPumpState    = false;
int  lastRaw          = 0;
int  lastMoisture     = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setPump(bool turnOn) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(relayPin, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(relayPin, turnOn ? HIGH : LOW);
  }
}

// -------------------------------------------------------------
// MQTT Incoming Message Callback (Executes instantly when backend publishes)
// -------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("[MQTT IN] Topic: ");
  Serial.print(topic);
  Serial.print(" | Payload: ");
  Serial.println(message);

  // Parse pump control command
  if (message.indexOf("\"pumpStatus\":true") >= 0) {
    setPump(true);
    currentPumpState = true;
    Serial.println("[BACKEND] Pump commanded ON immediately via MQTT");
  } else if (message.indexOf("\"pumpStatus\":false") >= 0) {
    setPump(false);
    currentPumpState = false;
    Serial.println("[BACKEND] Pump commanded OFF immediately via MQTT");
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
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

void reconnectMQTT() {
  // Loop until we're reconnected without blocking the rest of the board for long
  while (!mqttClient.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }
    Serial.print("Attempting MQTT connection... ");
    String clientId = "ESP8266-" + String(DEVICE_ID) + "-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected!");
      // Resubscribe to control topic
      mqttClient.subscribe(TOPIC_CONTROL);
      Serial.println("Subscribed to control topic.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 2 seconds...");
      delay(2000);
    }
  }
}

void publishReading(int raw, int moisture, bool pumpOn) {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  // Fast JSON payload
  String payload = "{\"device\":\"" + String(DEVICE_ID) +
                   "\",\"raw\":" + String(raw) +
                   ",\"moisture\":" + String(moisture) +
                   ",\"pump\":" + String(pumpOn ? "true" : "false") +
                   "}";

  boolean success = mqttClient.publish(TOPIC_TELEMETRY, payload.c_str());
  if (success) {
    Serial.println("[MQTT PUB OK] -> " + payload);
  } else {
    Serial.println("[MQTT PUB FAILED]");
  }
}

void setup() {
  pinMode(relayPin, OUTPUT);
  setPump(false);
  currentPumpState = false;
  lastPumpState    = false;

  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("ESP8266 Fast MQTT Pump Controller");
  Serial.println("========================================");

  connectWiFi();

  // Configure MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop(); // Keeps MQTT client alive and processes incoming messages instantly

  unsigned long now = millis();

  // --- Sample sensor & local logic ---
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

    if (currentPumpState != lastPumpState) {
      Serial.println(currentPumpState ? ">>> PUMP ON <<<" : ">>> PUMP OFF <<<");
      lastPumpState = currentPumpState;
    }
  }

  // --- Publish over MQTT (instant & lightweight) ---
  if (now - lastUploadTime >= uploadInterval) {
    lastUploadTime = now;
    publishReading(lastRaw, lastMoisture, currentPumpState);
  }
}