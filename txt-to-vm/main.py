import json
import logging
import os
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)

LISTEN_HOST = os.getenv("VM_LISTEN_HOST", "0.0.0.0")
LISTEN_PORT = int(os.getenv("VM_LISTEN_PORT", "2026"))
INGEST_PATH = os.getenv("VM_INGEST_PATH", "/ingest")
API_KEY = os.getenv("VM_API_KEY", "")
OUTPUT_DIR = Path(
    os.getenv(
        "VM_OUTPUT_DIR",
        r"C:\Users\instr_data_frontop\Desktop\geosonic-data\live",
    )
)

def output_file_for_today() -> Path:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    return OUTPUT_DIR / f"events-{datetime.now():%Y%m%d}.jsonl"


def normalize_payload(payload):
    if isinstance(payload, list):
        return payload
    if isinstance(payload, dict):
        return [payload]
    raise ValueError("JSON payload must be an object or an array")


class IngestHandler(BaseHTTPRequestHandler):
    def _send_json(self, status_code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
            logger.info("Responded %s to %s", status_code, self.client_address)
        except Exception:
            logger.exception("Failed while writing response to %s", self.client_address)

    def _read_json(self):
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length <= 0:
            raise ValueError("Request body is empty")

        raw_body = self.rfile.read(content_length)
        return json.loads(raw_body.decode("utf-8"))

    def _is_authorized(self):
        if not API_KEY:
            return True
        return self.headers.get("X-API-Key", "") == API_KEY

    def do_GET(self):
        try:
            logger.info("Handling GET %s from %s", self.path, self.client_address)
            if self.path == "/health":
                self._send_json(200, {"ok": True})
                return
            self._send_json(404, {"error": "not found"})
        except Exception as exc:
            logger.exception("Exception in do_GET")
            try:
                self._send_json(500, {"error": str(exc)})
            except Exception:
                pass

    def do_POST(self):
        logger.info("Handling POST %s from %s", self.path, self.client_address)
        if self.path != INGEST_PATH:
            self._send_json(404, {"error": "not found"})
            return

        if not self._is_authorized():
            self._send_json(401, {"error": "unauthorized"})
            return

        try:
            payload = self._read_json()
            events = normalize_payload(payload)
        except Exception as exc:
            logger.exception("Failed to parse request")
            self._send_json(400, {"error": str(exc)})
            return

        # Persist events as per-sensor text files named by device id (MAC)
        try:
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

            def sanitize_filename(name: str) -> str:
                # Replace characters that are invalid/undesirable in filenames
                return name.replace(':', '').replace(' ', '_')

            for event in events:
                if not isinstance(event, dict):
                    raise ValueError("Each event must be a JSON object")

                # Determine sensor id (prefer device_id, fall back to sensor_sn)
                sensor_id = event.get('device_id') or event.get('sensor_sn') or 'unknown_sensor'
                filename = sanitize_filename(sensor_id) + '.txt'
                out_path = OUTPUT_DIR / filename

                # Parse and format datetime to MM/DD/YY HH:MM:SS
                dt_str = event.get('datetime') or event.get('time') or ''
                dt_obj = None
                if dt_str:
                    try:
                        # Try ISO format first
                        dt_obj = datetime.fromisoformat(dt_str)
                    except Exception:
                        # Try common formats
                        for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S", "%m/%d/%y %H:%M:%S", "%m/%d/%Y %H:%M:%S"):
                            try:
                                dt_obj = datetime.strptime(dt_str, fmt)
                                break
                            except Exception:
                                continue
                if not dt_obj:
                    dt_obj = datetime.now()

                # Extract numeric fields in expected order and provide defaults
                def f(key):
                    try:
                        val = event.get(key)
                        if val is None:
                            return 0.0
                        return float(val)
                    except Exception:
                        return 0.0

                vppv = f('vppv')
                lppv = f('lppv')
                tppv = f('tppv')
                vf = f('vf')
                lf = f('lf')
                tf = f('tf')

                # Battery percent may be int or nested under 'battery_percent'
                battery = event.get('battery_percent')
                if battery is None:
                    battery = event.get('battery')
                try:
                    battery_val = int(float(battery)) if battery is not None else -1
                except Exception:
                    battery_val = -1

                # Line format: MM/DD/YY HH:MM:SS vppv lppv tppv vf lf tf battery
                line = f"{dt_obj:%m/%d/%y %H:%M:%S} {vppv:.2f} {lppv:.2f} {tppv:.2f} {vf:.2f} {lf:.2f} {tf:.2f} {battery_val}\n"

                # Append to sensor-specific file
                with out_path.open('a', encoding='utf-8') as fh:
                    fh.write(line)

        except Exception as exc:
            logger.exception("Failed to persist streamed events")
            self._send_json(500, {"error": str(exc)})
            return

        logger.info("Stored %d event(s) in %s", len(events), OUTPUT_DIR)
        self._send_json(200, {"ok": True, "stored": len(events)})

    def log_message(self, format, *args):
        logger.info("%s - %s", self.address_string(), format % args)


def main():
    server = ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), IngestHandler)
    logger.info("Listening on http://%s:%s%s", LISTEN_HOST, LISTEN_PORT, INGEST_PATH)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logger.info("Shutting down ingest server")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()