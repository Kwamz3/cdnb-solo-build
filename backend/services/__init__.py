from config.db import get_connection


def save_moisture(moisture):
    conn = get_connection()
    c = conn.cursor()
    c.execute('INSERT INTO sensor_data (moisture, pump_status) VALUES (?, ?)',
              (moisture, 'OFF'))  # Default to OFF, ESP32 handles auto-irrigation
    conn.commit()
    conn.close()


def get_latest_moisture():
    conn = get_connection()
    c = conn.cursor()
    c.execute('SELECT moisture, pump_status, timestamp FROM sensor_data ORDER BY id DESC LIMIT 1')
    row = c.fetchone()
    conn.close()
    return row


def set_pump_status(action):
    conn = get_connection()
    c = conn.cursor()
    # Update the most recent record's pump status
    c.execute('UPDATE sensor_data SET pump_status = ? WHERE id = (SELECT MAX(id) FROM sensor_data)', (action,))
    conn.commit()
    conn.close()
