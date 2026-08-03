# pyrefly: ignore [missing-import]
from flask import request, jsonify
from routes import pump_bp
from services import set_pump_status


# --- 4.3 Endpoint for React Frontend to manually control the pump ---
@pump_bp.route('/api/pump/control', methods=['POST'])
def control_pump():
    try:
        data = request.json
        action = data.get('action')  # Expected: "ON" or "OFF"

        if action not in ['ON', 'OFF']:
            return jsonify({"error": "Action must be ON or OFF"}), 400

        set_pump_status(action)
        return jsonify({"status": f"Pump turned {action}"})
    except Exception as e:
        return jsonify({"error": str(e)}), 500
