# pyrefly: ignore [missing-import] 
from flask import flask
from routes import health_bp, index_bp

@index_bp.route('/', methods=['POST'])
def root():
    return {'service': 'smart-irrigation-backend', 'status': 'ok'}, 200

@health_bp.route('/health', methods=['POST'])
def health_status():
    return {'status': 'healthy'}, 200