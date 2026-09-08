#!/usr/bin/env python3

import argparse
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse


DEFAULT_VENUE_ID = "venue-test-01"


def json_bytes(payload):
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


def valid_tokens():
    return {
        "root-token",
        "memory-summary-valid-token",
        os.environ.get("OWANALYTICS_TEST_VALID_TOKEN", "root-token"),
    }


def inventory_payload(serial_number):
    now = int(time.time())
    venue = os.environ.get("OWANALYTICS_TEST_VENUE_ID", DEFAULT_VENUE_ID)
    return {
        "id": serial_number,
        "name": serial_number,
        "description": "Memory summary integration test router",
        "created": now,
        "modified": now,
        "notes": [],
        "tags": [],
        "serialNumber": serial_number,
        "venue": venue,
        "entity": "",
        "subscriber": "",
        "deviceType": "ap",
        "qrCode": "",
        "geoCode": "",
        "location": "",
        "contact": "",
        "deviceConfiguration": "",
        "deviceRules": {
            "rcOnly": "inherit",
            "rrm": "inherit",
            "firmwareUpgrade": "inherit",
        },
        "managementPolicy": "",
        "state": "active",
        "devClass": "",
        "locale": "en-US",
        "realMacAddress": serial_number,
        "doNotAllowOverrides": False,
        "imported": 0,
        "connected": 0,
        "platform": "AP",
    }


def venue_payload(venue):
    return {
        "id": venue,
        "name": "Memory Summary Test Venue",
        "description": "Integration test venue",
        "devices": [],
        "parent": "",
        "entity": "",
        "children": [],
        "topology": [],
        "design": {},
        "managementPolicy": "",
        "deviceConfiguration": [],
        "contacts": [],
        "location": "",
        "deviceRules": {
            "rcOnly": "inherit",
            "rrm": "inherit",
            "firmwareUpgrade": "inherit",
        },
        "sourceIP": [],
        "variables": [],
        "managementPolicies": [],
        "managementRoles": [],
        "maps": [],
        "configurations": [],
        "boards": [],
        "created": 0,
        "modified": 0,
        "notes": [],
        "tags": [],
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def send_json(self, status, payload):
        body = json_bytes(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        prefix = "/api/v1/inventory/"
        if parsed.path.startswith(prefix):
            authorization = self.headers.get("Authorization", "")
            if authorization.removeprefix("Bearer ") not in valid_tokens():
                self.send_json(404, {"error": "not_found"})
                return

            serial_number = unquote(parsed.path[len(prefix):])
            if not serial_number:
                self.send_json(404, {"error": "not_found"})
                return

            self.send_json(200, inventory_payload(serial_number))
            return

        venue_prefix = "/api/v1/venue/"
        if parsed.path.startswith(venue_prefix):
            venue = unquote(parsed.path[len(venue_prefix):])
            if not venue:
                self.send_json(404, {"error": "not_found"})
                return

            self.send_json(200, venue_payload(venue))
            return

        self.send_json(404, {"error": "not_found"})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18081)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Fake OWPROV running at http://{args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
