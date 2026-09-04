# ============================================================
# SMART IRRIGATION BACKEND - Python / Flask-SocketIO + MQTT
# ============================================================
import os
import json
import threading
import time
from datetime import datetime, timezone
#pyrefly:ignore[missing-import]
from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO, emit
# pyrefly: ignore [missing-import]
import paho.mqtt.client as mqtt

from config.db import init_db, get_history_records
from services import (
    system_state,
    process_telemetry,
    update_controls,
    fetch_weather_forecast,
    check_system_alerts,
    send_webhook_alert
)
from routes import moisture_bp, pump_bp

app = Flask(__name__)
CORS(app, resources={r"/*": {"origins": "*"}})
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# ============================================================
# MQTT CONFIGURATION
# ============================================================
# You can use a local broker, or free cloud brokers:
# - "broker.hivemq.com" (Public, no auth needed for testing)
# - "broker.emqx.io"
# - Or your own private Mosquitto / Cloud MQTT broker
MQTT_BROKER_HOST = os.environ.get("MQTT_HOST", "broker.hivemq.com")
MQTT_BROKER_PORT = int(os.environ.get("MQTT_PORT", 1883))
MQTT_TOPIC_TELEMETRY = "irrigation/pump-01/telemetry"
MQTT_TOPIC_CONTROL   = "irrigation/pump-01/control"

mqtt_client = mqtt.Client(client_id="smart-irrigation-backend-server")


def publish_pump_command(pump_on: bool):
    """Publish pump command to ESP8266 over MQTT instantly."""
    payload = json.dumps({"pumpStatus": pump_on})
    try:
        mqtt_client.publish(MQTT_TOPIC_CONTROL, payload, qos=1)
        print(f"[MQTT PUB] -> {MQTT_TOPIC_CONTROL}: {payload}")
    except Exception as e:
        print(f"[MQTT PUB] Error: {e}")


def on_mqtt_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Connected successfully to {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
        client.subscribe(MQTT_TOPIC_TELEMETRY)
        print(f"[MQTT] Subscribed to {MQTT_TOPIC_TELEMETRY}")
    else:
        print(f"[MQTT] Connection failed with code {rc}")


def on_mqtt_message(client, userdata, msg):
    """Handle telemetry published by ESP8266 in real time."""
    try:
        payload_str = msg.payload.decode("utf-8")
        data = json.loads(payload_str)
        print(f"[MQTT RX] <- {data}")

        moisture = data.get("moisture")
        if moisture is not None:
            old_pump_status = system_state["pumpStatus"]
            
            # Process through the existing business logic pipeline
            updated_state = process_telemetry(
                moisture=float(moisture),
                temperature=float(data.get("temperature", system_state["temperature"])),
                water_level=float(data.get("waterLevel", data.get("water_level", system_state["waterLevel"])))
            )

            # Broadcast to React frontend via WebSockets
            socketio.emit('telemetryUpdate', updated_state)

            # If auto-mode changed the pump status, command the hardware immediately
            if updated_state["pumpStatus"] != old_pump_status:
                publish_pump_command(updated_state["pumpStatus"])

    except Exception as e:
        print(f"[MQTT RX Error] {e}")


mqtt_client.on_connect = on_mqtt_connect
mqtt_client.on_message = on_mqtt_message


def start_mqtt_client():
    try:
        mqtt_client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        mqtt_client.loop_start()  # Runs in its own background thread
    except Exception as e:
        print(f"[MQTT] Failed to start MQTT background client: {e}")


# ------------------------------------------------------------
# Health & Status
# ------------------------------------------------------------
@app.get('/')
def root():
    return {
        'service': 'smart-irrigation-backend',
        'version': '3.0.0 (MQTT-Enabled)',
        'status': 'ok',
        'socketio': True
    }, 200


@app.get('/health')
def health_status():
    return {'status': 'healthy'}, 200


# ------------------------------------------------------------
# Controls Endpoint (Called when user clicks Start/Stop on Dashboard)
# ------------------------------------------------------------
@app.post('/api/controls')
def set_controls():
    data = request.get_json(silent=True) or {}
    pump_status = data.get('pumpStatus')
    auto_mode = data.get('autoMode')
    threshold = data.get('threshold')
    webhook_url = data.get('webhookUrl')

    updated_state = update_controls(
        pump_status=pump_status,
        auto_mode=auto_mode,
        threshold=threshold,
        webhook_url=webhook_url
    )

    # 1. Update React UI via WebSocket
    socketio.emit('telemetryUpdate', updated_state)

    # 2. Instantly notify ESP8266 via MQTT (<50ms)
    publish_pump_command(updated_state["pumpStatus"])

    return jsonify({
        "status": "updated",
        "systemState": updated_state
    }), 200


# ------------------------------------------------------------
# System State & History Endpoints
# ------------------------------------------------------------
@app.get('/api/state')
def get_state():
    return jsonify(system_state), 200


@app.get('/api/history')
def get_history():
    limit = request.args.get('limit', default=30, type=int)
    records = get_history_records(limit=limit)
    return jsonify(records), 200


# ------------------------------------------------------------
# Weather Forecast Integration (Rain Skip)
# ------------------------------------------------------------
@app.get('/api/weather')
def get_weather():
    forecast = fetch_weather_forecast()
    return jsonify({
        "status": "success",
        "forecast": forecast,
        "rainExpected": system_state["rainExpected"],
        "condition": system_state["weatherCondition"],
        "rainProbability": system_state["rainProbability"]
    }), 200


# ------------------------------------------------------------
# Alert & Webhook Management
# ------------------------------------------------------------
@app.post('/api/alerts/test')
def test_alert():
    data = request.get_json(silent=True) or {}
    webhook_url = data.get('webhookUrl') or system_state.get('webhookUrl')
    if webhook_url:
        system_state['webhookUrl'] = webhook_url
    send_webhook_alert("TEST_ALERT", "Test alert triggered from Smart Irrigation Dashboard.")
    return jsonify({"status": "test_alert_dispatched", "webhookUrl": system_state.get('webhookUrl')}), 200


# ------------------------------------------------------------
# Socket.IO Event Handlers
# ------------------------------------------------------------
@socketio.on('connect')
def handle_connect():
    emit('telemetryUpdate', system_state)


@socketio.on('requestTelemetry')
def handle_request_telemetry():
    emit('telemetryUpdate', system_state)


# ------------------------------------------------------------
# Periodic Background Tasks
# ------------------------------------------------------------
def background_monitor():
    while True:
        try:
            fetch_weather_forecast()
            alerts = check_system_alerts()
            if alerts:
                socketio.emit('telemetryUpdate', system_state)
        except Exception as e:
            print(f"[Background Monitor] Error: {e}")
        time.sleep(300)


app.register_blueprint(moisture_bp)
app.register_blueprint(pump_bp)


if __name__ == '__main__':
    init_db()
    
    # Start MQTT background loop
    start_mqtt_client()

    threading.Thread(target=fetch_weather_forecast, daemon=True).start()
    threading.Thread(target=background_monitor, daemon=True).start()

    print("==================================================")
    print(" Smart Irrigation Backend (MQTT + Socket.IO)      ")
    print(" Running on http://0.0.0.0:5000                   ")
    print("==================================================")
    port = int(os.environ.get("PORT", 5000))
    socketio.run(app, host='0.0.0.0', port=port, debug=False, allow_unsafe_werkzeug=True)