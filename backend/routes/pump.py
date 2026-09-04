# ============================================================
# PUMP ROUTES (Legacy & Direct compatibility)
# ============================================================
# pyrefly: ignore[missing-import]
from flask import request, jsonify
from routes import pump_bp
from services import update_controls


@pump_bp.route('/api/pump/control', methods=['POST'])
def control_pump():
    try:
        data = request.get_json(silent=True) or {}
        action = data.get('action')  # Expected: "ON" or "OFF"

        if action not in ['ON', 'OFF']:
            return jsonify({"error": "Action must be ON or OFF"}), 400

        updated = update_controls(pump_status=(action == 'ON'), auto_mode=False)

        try:
            from server import socketio
            socketio.emit('telemetryUpdate', updated)
        except Exception:
            pass

        return jsonify({
            "status": f"Pump turned {action}",
            "systemState": updated
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500
