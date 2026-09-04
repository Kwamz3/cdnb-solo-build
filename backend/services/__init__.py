import time
import requests
from datetime import datetime
from config.db import save_telemetry_record, get_latest_record, get_history_records

# In-memory store for latest hardware and system states
system_state = {
    "moisture": 0.0,
    "temperature": 0.0,
    "waterLevel": 100.0,
    "pumpStatus": False,
    "autoMode": True,
    "threshold": 30.0,        # Turn on pump if moisture < 30%
    "rainExpected": False,
    "rainProbability": 0,
    "weatherCondition": "Sunny / Clear",
    "weatherLocation": "Accra, GH",
    "latitude": 5.6037,
    "longitude": -0.1870,
    "webhookUrl": "",
    "alerts": [],
    "lastTelemetry": None,
    "sensorOnline": False
}

# Keep track of last alert timestamp to avoid spamming webhooks
_last_webhook_trigger = 0


def fetch_weather_forecast():
    """
    Fetch local weather and rain forecast via Open-Meteo API.
    Zero-config & free (no API key needed).
    """
    lat = system_state["latitude"]
    lon = system_state["longitude"]
    url = (
        f"https://api.open-meteo.com/v1/forecast?"
        f"latitude={lat}&longitude={lon}&current=temperature_2m,precipitation,weather_code"
        f"&hourly=precipitation_probability,precipitation&forecast_days=1&timezone=auto"
    )

    try:
        resp = requests.get(url, timeout=5)
        if resp.status_code == 200:
            data = resp.json()
            hourly_prob = data.get("hourly", {}).get("precipitation_probability", [])
            hourly_precip = data.get("hourly", {}).get("precipitation", [])

            # Check next 12 hours for rain
            next_12h_probs = hourly_prob[:12] if hourly_prob else [0]
            next_12h_precip = hourly_precip[:12] if hourly_precip else [0.0]

            max_prob = max(next_12h_probs) if next_12h_probs else 0
            total_expected_rain = sum(next_12h_precip) if next_12h_precip else 0.0

            # If probability >= 45% or expected rain > 0.5mm, flag rain expected
            rain_expected = (max_prob >= 45) or (total_expected_rain > 0.5)

            current_temp = data.get("current", {}).get("temperature_2m")
            weather_code = data.get("current", {}).get("weather_code", 0)

            conditions_map = {
                0: "Clear sky",
                1: "Mainly clear",
                2: "Partly cloudy",
                3: "Overcast",
                51: "Light drizzle",
                61: "Slight rain",
                63: "Moderate rain",
                65: "Heavy rain",
                80: "Rain showers",
                95: "Thunderstorm"
            }
            condition = conditions_map.get(weather_code, "Partly cloudy" if rain_expected else "Clear sky")

            system_state["rainExpected"] = rain_expected
            system_state["rainProbability"] = max_prob
            system_state["weatherCondition"] = condition

            return {
                "rainExpected": rain_expected,
                "rainProbability": max_prob,
                "condition": condition,
                "currentTemp": current_temp,
                "expectedPrecipMm": round(total_expected_rain, 2)
            }
    except Exception as err:
        print(f"[Weather API] Forecast fetch error: {err}")

    return {
        "rainExpected": system_state["rainExpected"],
        "rainProbability": system_state["rainProbability"],
        "condition": system_state["weatherCondition"]
    }


def send_webhook_alert(alert_type, message):
    """
    Sends an automated webhook alert (Slack, Discord, Twilio, Zapier, Webhook relay)
    """
    global _last_webhook_trigger
    now = time.time()
    # Throttle alerts to at most once per 60 seconds
    if now - _last_webhook_trigger < 60:
        return

    webhook_url = system_state.get("webhookUrl")
    if not webhook_url:
        return

    payload = {
        "event": "SMART_IRRIGATION_ALERT",
        "type": alert_type,
        "message": message,
        "timestamp": datetime.utcnow().isoformat(),
        "state": {
            "moisture": system_state["moisture"],
            "temperature": system_state["temperature"],
            "waterLevel": system_state["waterLevel"],
            "pumpStatus": system_state["pumpStatus"]
        }
    }

    try:
        requests.post(webhook_url, json=payload, timeout=4)
        _last_webhook_trigger = now
    except Exception as e:
        print(f"[Alert Webhook] Failed to deliver webhook: {e}")


def check_system_alerts():
    """
    Check for abnormal drops in water level or sensor disconnects
    """
    alerts = []
    # Water level alerts
    if system_state["waterLevel"] <= 15.0:
        msg = f"CRITICAL: Reservoir water level is critically low ({system_state['waterLevel']}%). Please refill immediately!"
        alerts.append({"type": "WATER_CRITICAL", "message": msg, "severity": "danger"})
        send_webhook_alert("WATER_CRITICAL", msg)
    elif system_state["waterLevel"] <= 30.0:
        msg = f"WARNING: Water level is low ({system_state['waterLevel']}%)."
        alerts.append({"type": "WATER_LOW", "message": msg, "severity": "warning"})

    # Sensor disconnect check
    last_seen = system_state.get("lastTelemetry")
    if last_seen and (time.time() - last_seen > 30):
        system_state["sensorOnline"] = False
        msg = "WARNING: Sensor hardware disconnect detected. No telemetry received for > 30s."
        alerts.append({"type": "SENSOR_DISCONNECTED", "message": msg, "severity": "danger"})
        send_webhook_alert("SENSOR_DISCONNECTED", msg)
    elif last_seen:
        system_state["sensorOnline"] = True

    system_state["alerts"] = alerts
    return alerts


def process_telemetry(moisture, temperature=None, water_level=None):
    """
    Process incoming hardware telemetry:
    - updates in-memory system_state
    - evaluates auto-irrigation logic (including weather rain-skip)
    - records to database
    - checks alert triggers
    """
    system_state["moisture"] = float(moisture)
    if temperature is not None:
        system_state["temperature"] = float(temperature)
    if water_level is not None:
        system_state["waterLevel"] = float(water_level)

    system_state["lastTelemetry"] = time.time()
    system_state["sensorOnline"] = True

    # Auto-irrigation decision logic
    if system_state["autoMode"]:
        # Safety check: do not turn pump on if water reservoir is empty
        if system_state["waterLevel"] <= 5.0:
            system_state["pumpStatus"] = False
        # Weather Integration: skip watering if rain is forecasted
        elif system_state["rainExpected"]:
            system_state["pumpStatus"] = False
        else:
            # Turn on pump if moisture is below threshold
            system_state["pumpStatus"] = (system_state["moisture"] < system_state["threshold"])

    # Persist record in SQLite
    pump_str = "ON" if system_state["pumpStatus"] else "OFF"
    try:
        save_telemetry_record(
            moisture=system_state["moisture"],
            temperature=system_state["temperature"],
            pump_status=pump_str,
            water_level=system_state["waterLevel"],
            rain_expected=1 if system_state["rainExpected"] else 0
        )
    except Exception as e:
        print(f"[DB Save] Error: {e}")

    check_system_alerts()
    return system_state


def update_controls(pump_status=None, auto_mode=None, threshold=None, webhook_url=None):
    """
    Updates system controls from frontend or manual override
    """
    if pump_status is not None:
        if isinstance(pump_status, str):
            system_state["pumpStatus"] = (pump_status.upper() == "ON" or pump_status.lower() == "true")
        else:
            system_state["pumpStatus"] = bool(pump_status)

    if auto_mode is not None:
        system_state["autoMode"] = bool(auto_mode)

    if threshold is not None:
        system_state["threshold"] = float(threshold)

    if webhook_url is not None:
        system_state["webhookUrl"] = str(webhook_url)

    # Re-evaluate auto mode if autoMode was just enabled
    if system_state["autoMode"]:
        if system_state["waterLevel"] <= 5.0 or system_state["rainExpected"]:
            system_state["pumpStatus"] = False
        else:
            system_state["pumpStatus"] = (system_state["moisture"] < system_state["threshold"])

    return system_state


# Legacy service bridges for backward compatibility
def save_moisture(moisture):
    return process_telemetry(moisture=moisture)


def get_latest_moisture():
    rec = get_latest_record()
    if rec:
        return (rec['moisture'], rec['pump_status'], rec['timestamp'])
    return None


def set_pump_status(action):
    turn_on = (action.upper() == "ON")
    update_controls(pump_status=turn_on, auto_mode=False)
