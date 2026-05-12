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
LISTEN_PORT = int(os.getenv("VM_LISTEN_PORT", "8080"))
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
        self.wfile.write(body)

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
        if self.path == "/health":
            self._send_json(200, {"ok": True})
            return
        self._send_json(404, {"error": "not found"})

    def do_POST(self):
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

        output_file = output_file_for_today()
        try:
            with output_file.open("a", encoding="utf-8") as handle:
                for event in events:
                    if not isinstance(event, dict):
                        raise ValueError("Each event must be a JSON object")
                    handle.write(json.dumps(event, sort_keys=True) + "\n")
        except Exception as exc:
            logger.exception("Failed to persist streamed events")
            self._send_json(500, {"error": str(exc)})
            return

        logger.info("Stored %d event(s) in %s", len(events), output_file)
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