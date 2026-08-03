# pyrefly: ignore [missing-import]
from flask import Flask
from flask_cors import CORS

from config.db import init_db
from routes import moisture_bp, pump_bp


def create_app():
    app = Flask(__name__)
    CORS(app)  # Allows the React frontend (localhost:5173) to communicate with this backend

    app.register_blueprint(moisture_bp)
    app.register_blueprint(pump_bp)
    return app


if __name__ == '__main__':
    init_db()
    app = create_app()
    print("Starting Smart Irrigation Backend on http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=True)
