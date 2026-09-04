# ============================================================
# SMART IRRIGATION BACKEND - Python / Flask-SocketIO
# ============================================================
# pyrefly: ignore [missing-import]
from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_socketio import SocketIO, emit
import threading
import time
import os
from datetime import datetime, timezone
import logging

# Enable logging to see what's happening
logging.basicConfig(level=logging.DEBUG)

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

# More permissive CORS
CORS(app, resources={
    r"/*": {
        "origins": "*",
        "methods": ["GET", "POST", "PUT", "DELETE", "OPTIONS"],
        "allow_headers": ["Content-Type", "Authorization"]
    }
})

# SocketIO with more permissive settings
socketio = SocketIO(
    app, 
    cors_allowed_origins="*", 
    async_mode='threading',
    logger=True,
    engineio_logger=True
)

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

# ------------------------------------------------------------
# 1. IoT Telemetry Endpoint
# ------------------------------------------------------------
@app.post('/api/telemetry')
def receive_telemetry():
    try:
        data = request.get_json(silent=True) or {}
        print(f"[TELEMETRY] Received: {data}")
        
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

        # Force emit with broadcast=True
        socketio.emit('telemetryUpdate', updated_state, broadcast=True)
        print(f"[TELEMETRY] Broadcast sent: moisture={updated_state['moisture']}%, pump={updated_state['pumpStatus']}")

        return jsonify({
            "status": "success",
            "pumpStatus": updated_state["pumpStatus"],
            "systemState": updated_state
        }), 200
    except Exception as e:
        print(f"[TELEMETRY] Error: {e}")
        return jsonify({"error": str(e)}), 500

# ------------------------------------------------------------
# 2. Controls Endpoint
# ------------------------------------------------------------
@app.post('/api/controls')
def set_controls():
    try:
        data = request.get_json(silent=True) or {}
        print(f"[CONTROLS] Received: {data}")
        
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

        socketio.emit('telemetryUpdate', updated_state, broadcast=True)

        return jsonify({
            "status": "updated",
            "systemState": updated_state
        }), 200
    except Exception as e:
        print(f"[CONTROLS] Error: {e}")
        return jsonify({"error": str(e)}), 500

# ------------------------------------------------------------
# 3. System State & History
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
# 4. Arduino Readings Endpoint - FIXED
# ------------------------------------------------------------
readings = []

@app.post("/api/readings")
def add_reading():
    """Endpoint for ESP8266 firmware (POST /api/readings)."""
    try:
        data = request.get_json(force=True) or {}
        print(f"[READINGS] Raw data from Arduino: {data}")
        
        # Add timestamp
        data["receivedAt"] = datetime.now(timezone.utc).isoformat()
        readings.append(data)
        
        # Extract moisture - CRITICAL: Arduino sends 'moisture' field
        moisture = data.get("moisture")
        
        if moisture is None:
            print(f"[READINGS] ERROR: No moisture field in {data}")
            return jsonify({"error": "Missing moisture field", "received": data}), 400
        
        # Convert to float
        moisture_val = float(moisture)
        print(f"[READINGS] Moisture: {moisture_val}%")
        
        # Get other values
        pump_from_arduino = data.get("pump", False)
        temp_from_arduino = data.get("temperature", system_state["temperature"])
        water_from_arduino = data.get("waterLevel", data.get("water_level", system_state["waterLevel"]))
        
        # Process telemetry - this updates system_state
        updated_state = process_telemetry(
            moisture=moisture_val,
            temperature=float(temp_from_arduino),
            water_level=float(water_from_arduino)
        )
        
        # If in manual mode, respect Arduino's pump state
        if not system_state["autoMode"]:
            updated_state["pumpStatus"] = bool(pump_from_arduino)
            system_state["pumpStatus"] = bool(pump_from_arduino)
        
        # CRITICAL: Broadcast to frontend
        print(f"[READINGS] Broadcasting: moisture={updated_state['moisture']}%, pump={updated_state['pumpStatus']}")
        socketio.emit('telemetryUpdate', updated_state, broadcast=True)
        
        return jsonify({
            "status": "success",
            "moisture": moisture_val,
            "pumpStatus": updated_state["pumpStatus"],
            "systemState": updated_state
        }), 200
        
    except Exception as e:
        print(f"[READINGS] ERROR: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({"error": str(e)}), 500

@app.get("/api/readings")
def list_readings():
    return jsonify(readings[-100:])

# ------------------------------------------------------------
# 5. Weather
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
# 6. Alerts
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
    print('[Socket.IO] Client connected')
    # Send current state immediately
    emit('telemetryUpdate', system_state)

@socketio.on('disconnect')
def handle_disconnect():
    print('[Socket.IO] Client disconnected')

@socketio.on('requestTelemetry')
def handle_request_telemetry():
    print('[Socket.IO] Client requested telemetry')
    emit('telemetryUpdate', system_state)

# ------------------------------------------------------------
# Background Tasks
# ------------------------------------------------------------
def background_monitor():
    while True:
        try:
            fetch_weather_forecast()
            alerts = check_system_alerts()
            if alerts:
                socketio.emit('telemetryUpdate', system_state, broadcast=True)
        except Exception as e:
            print(f"[Background] Error: {e}")
        time.sleep(300)

# Register blueprints
app.register_blueprint(moisture_bp)
app.register_blueprint(pump_bp)

if __name__ == '__main__':
    init_db()
    threading.Thread(target=fetch_weather_forecast, daemon=True).start()
    threading.Thread(target=background_monitor, daemon=True).start()

    print("==================================================")
    print(" Smart Irrigation Backend (Python / Socket.IO)    ")
    print(" Running on http://localhost:5000                 ")
    print("==================================================")
    
    port = int(os.environ.get("PORT", 5000))
    socketio.run(app, host='0.0.0.0', port=port, debug=True, allow_unsafe_werkzeug=True)