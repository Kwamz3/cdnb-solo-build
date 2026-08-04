# pyrefly: ignore[missing-import]
from flask import Blueprint

moisture_bp = Blueprint('moisture', __name__)
pump_bp = Blueprint('pump', __name__)

from . import moisture, pump   # noqa: E402, F401
