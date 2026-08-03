#pyrefly: ignore[missing-import]
from flask import request, jsonify
from routes import moisture_bp
from services import save_moisture, get_latest_moisture as get_latest_moisture_data


# --- 4.1 Endpoint for ESP32 to send moisture data ---
@moisture_bp.route('/api/moisture', methods=['POST'])
def receive_moisture():
    try:
        data = request.json
        moisture = data.get('moisture')

        if moisture is None:
            return jsonify({"error": "Moisture value required"}), 400

        save_moisture(moisture)
        return jsonify({"status": "success", "moisture": moisture})
    except Exception as e:
        return jsonify({"error": str(e)}), 500


# --- 4.2 Endpoint for React Frontend to get the latest data ---
@moisture_bp.route('/api/moisture/latest', methods=['GET'])
def get_latest_moisture():
    try:
        row = get_latest_moisture_data()

        if row:
            return jsonify({
                "moisture": row[0],
                "pump_status": row[1],
                "timestamp": row[2]
            })
        return jsonify({"error": "No data available yet"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500
