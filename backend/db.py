# Re-export from config.db for backward compatibility
from config.db import (
    get_connection,
    init_db,
    save_telemetry_record,
    get_latest_record,
    get_history_records,
    DB_PATH
)

__all__ = [
    'get_connection',
    'init_db',
    'save_telemetry_record',
    'get_latest_record',
    'get_history_records',
    'DB_PATH'
]
