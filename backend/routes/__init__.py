# pyrefly: ignore[missing-import]
from flask import Blueprint

moisture_bp = Blueprint('moisture', __name__)
pump_bp = Blueprint('pump', __name__)
health_bp = Blueprint('health', __name__)
index_bp = Blueprint('index', __name__)

from . import moisture, pump, health, index   # noqa: E402, F401
