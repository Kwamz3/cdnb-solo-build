import sqlite3
import os

DB_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'irrigation.db')


def get_connection():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = get_connection()
    c = conn.cursor()
    # Create table if it doesn't exist
    c.execute('''CREATE TABLE IF NOT EXISTS sensor_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        moisture REAL,
        temperature REAL DEFAULT 0.0,
        water_level REAL DEFAULT 100.0,
        pump_status TEXT DEFAULT 'OFF',
        rain_expected INTEGER DEFAULT 0,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )''')

    # Add any missing columns to existing database safely
    c.execute("PRAGMA table_info(sensor_data)")
    columns = [col[1] for col in c.fetchall()]

    if 'temperature' not in columns:
        c.execute("ALTER TABLE sensor_data ADD COLUMN temperature REAL DEFAULT 0.0")
    if 'water_level' not in columns:
        c.execute("ALTER TABLE sensor_data ADD COLUMN water_level REAL DEFAULT 100.0")
    if 'rain_expected' not in columns:
        c.execute("ALTER TABLE sensor_data ADD COLUMN rain_expected INTEGER DEFAULT 0")

    conn.commit()
    conn.close()


def save_telemetry_record(moisture, temperature=0.0, pump_status='OFF', water_level=100.0, rain_expected=0):
    conn = get_connection()
    c = conn.cursor()
    c.execute('''INSERT INTO sensor_data (moisture, temperature, pump_status, water_level, rain_expected)
                 VALUES (?, ?, ?, ?, ?)''',
              (moisture, temperature, pump_status, water_level, 1 if rain_expected else 0))
    conn.commit()
    conn.close()


def get_latest_record():
    conn = get_connection()
    c = conn.cursor()
    c.execute('SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1')
    row = c.fetchone()
    conn.close()
    if row:
        return dict(row)
    return None


def get_history_records(limit=30):
    conn = get_connection()
    c = conn.cursor()
    c.execute('SELECT * FROM sensor_data ORDER BY id DESC LIMIT ?', (limit,))
    rows = c.fetchall()
    conn.close()
    # Return chronological order (oldest first for chart)
    return [dict(r) for r in reversed(rows)]


# Ensure table and schema exist on load
try:
    init_db()
except Exception:
    pass
