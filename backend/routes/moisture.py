# ============================================================
# MOISTURE ROUTES (Legacy & Direct compatibility)
# ============================================================
# pyrefly: ignore [missing-import]
from flask import request, jsonify
from routes import moisture_bp
from services import process_telemetry, system_state, get_latest_moisture as get_latest_moisture_data


@moisture_bp.route('/api/moisture', methods=['POST'])
def receive_moisture():
    try:
        data = request.get_json(silent=True) or {}
        moisture = data.get('moisture')

        if moisture is None:
            return jsonify({"error": "Moisture value required"}), 400

        temperature = data.get('temperature', system_state["temperature"])
        water_level = data.get('water_level', system_state["waterLevel"])

        updated = process_telemetry(moisture, temperature, water_level)

        # Broadcast if SocketIO is active
        try:
            from server import socketio
            socketio.emit('telemetryUpdate', updated)
        except Exception:
            pass

        return jsonify({
            "status": "success",
            "moisture": moisture,
            "pumpStatus": updated["pumpStatus"]
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@moisture_bp.route('/api/moisture/latest', methods=['GET'])
def get_latest_moisture():
    try:
        row = get_latest_moisture_data()
        if row:
            return jsonify({
                "moisture": row[0],
                "pump_status": row[1],
                "timestamp": row[2]
            }), 200
        return jsonify({"error": "No data available yet"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500
