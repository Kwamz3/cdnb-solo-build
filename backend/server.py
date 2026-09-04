# ============================================================
# SMART IRRIGATION BACKEND - Python / Flask-SocketIO
# ============================================================
#pyrefly: ignore[missing-import]
from requests import models
from Flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO, emit
import threading
import time
import os
from datetime import datetime, timezone

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
app.config['SECRET_KEY'] = 'smart-irrigation-secret-key-2026'

# Allow cross-origin requests from React dashboard (Vite / localhost / Netlify)
CORS(app, resources={r"/*": {"origins": "*"}})
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')


# ------------------------------------------------------------
# Health & Status
# ------------------------------------------------------------
@app.get('/')
def root():
    return {
        'service': 'smart-irrigation-backend',
        'version': '2.0.0',
        'status': 'ok',
        'socketio': True
    }, 200


@app.get('/health')
def health_status():
    return {'status': 'healthy'}, 200


    port = int(os.environ.get("PORT", 5000))
    app.run(host="0.0.0.0", port=port)


# ------------------------------------------------------------
# 1. IoT Telemetry Endpoint
# Hardware (ESP8266 / ESP32) or simulator posts telemetry here
# ------------------------------------------------------------
@app.post('/api/telemetry')
def receive_telemetry():
    data = request.get_json(silent=True) or {}
    moisture = data.get('moisture')

    if moisture is None:
        return jsonify({"error": "Moisture value is required"}), 400

    temperature = data.get('temperature', system_state["temperature"])
    water_level = data.get('waterLevel', data.get('water_level', system_state["waterLevel"]))

    updated_state = process_telemetry(
        moisture=moisture,
        temperature=temperature,
        water_level=water_level
    )

    # Broadcast updated state to all connected frontend clients via WebSockets
    socketio.emit('telemetryUpdate', updated_state)

    return jsonify({
        "status": "success",
        "pumpStatus": updated_state["pumpStatus"],
        "systemState": updated_state
    }), 200


# ------------------------------------------------------------
# 2. Controls Endpoint
# Frontend or remote app updates pump status, autoMode, or threshold
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

    # Broadcast new system state to all connected frontend clients
    socketio.emit('telemetryUpdate', updated_state)

    return jsonify({
        "status": "updated",
        "systemState": updated_state
    }), 200


# ------------------------------------------------------------
# 3. System State & History Endpoints
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
# 4. Irrigation System readings
# ------------------------------------------------------------
readings = []  # in-memory store; swap for a database in production


@app.post("/api/readings")
def add_reading():
    data = request.get_json(force=True)
    # The ESP8266 has no RTC, so let the backend timestamp each reading
    data["receivedAt"] = datetime.now(timezone.utc).isoformat()
    readings.append(data)
    print(data)
    return jsonify({"ok": True})


@app.get("/api/readings")
def list_readings():
    # Return the most recent 100 readings as a quick sanity check
    return jsonify(readings[-100:])


# ------------------------------------------------------------
# 5. Weather Forecast Integration (Rain Skip)
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
# 6. Alert & Webhook Management
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
    print('[Socket.IO] Client connected to live telemetry stream')
    # Immediately send current state on connection
    emit('telemetryUpdate', system_state)


@socketio.on('disconnect')
def handle_disconnect():
    print('[Socket.IO] Client disconnected')


@socketio.on('requestTelemetry')
def handle_request_telemetry():
    emit('telemetryUpdate', system_state)


# ------------------------------------------------------------
# Periodic Background Tasks (Weather refresh & Sensor timeout)
# ------------------------------------------------------------
def background_monitor():
    """Periodically refreshes weather and checks for sensor heartbeats."""
    while True:
        try:
            fetch_weather_forecast()
            alerts = check_system_alerts()
            if alerts:
                socketio.emit('telemetryUpdate', system_state)
        except Exception as e:
            print(f"[Background Monitor] Error: {e}")
        time.sleep(300)  # Every 5 minutes


# Register existing legacy blueprints
app.register_blueprint(moisture_bp)
app.register_blueprint(pump_bp)


if __name__ == '__main__':
    init_db()
    # Fetch initial weather forecast
    threading.Thread(target=fetch_weather_forecast, daemon=True).start()
    threading.Thread(target=background_monitor, daemon=True).start()

    print("==================================================")
    print(" Smart Irrigation Backend (Python / Socket.IO)    ")
    print(" Running on http://localhost:5000                 ")
    print("==================================================")
    socketio.run(app, host='0.0.0.0', port=5000, debug=True, allow_unsafe_werkzeug=True)
