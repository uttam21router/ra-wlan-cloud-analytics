"""
Integration tests for GET /api/v1/devices/{routerId}/wifi-clients/rssi-summary.

These tests run against a live OWANALYTICS process with fake OWSEC/OWPROV
services, matching the other MCP integration test setups.

Required environment:
  OWANALYTICS_TEST_URL
  OWANALYTICS_TEST_DB_DSN

Optional environment:
  OWANALYTICS_TEST_VALID_TOKEN
  OWANALYTICS_TEST_ROUTER_ID
  OWANALYTICS_TEST_BOARD_ID
  OWANALYTICS_TEST_VENUE_ID
"""

from __future__ import annotations

import json
import os
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any

import pytest


pytestmark = pytest.mark.integration

DEFAULT_ROUTER_ID = "60cf84f22290"
UNKNOWN_ROUTER_ID = "60cf84f22291"
UNAUTHORIZED_ROUTER_ID = "60cf84f22292"
INVALID_RESPONSE_ROUTER_ID = "60cf84f22293"
DEFAULT_BOARD_ID = "board-test-01"
DEFAULT_VENUE_ID = "venue-test-01"
OTHER_BOARD_ID = "rssi-other-board"
OTHER_VENUE_ID = "rssi-other-venue"
DEFAULT_VALID_TOKEN = "root-token"


def env_or_skip(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        pytest.skip(f"{name} is required for rssi-summary integration tests")
    return value


def router_id() -> str:
    return os.environ.get("OWANALYTICS_TEST_ROUTER_ID", DEFAULT_ROUTER_ID)


def board_id() -> str:
    return os.environ.get("OWANALYTICS_TEST_BOARD_ID", DEFAULT_BOARD_ID)


def venue_id() -> str:
    return os.environ.get("OWANALYTICS_TEST_VENUE_ID", DEFAULT_VENUE_ID)


def valid_token() -> str:
    return os.environ.get("OWANALYTICS_TEST_VALID_TOKEN", DEFAULT_VALID_TOKEN)


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def format_utc(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def utc_epoch(value: str) -> int:
    return int(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp())


@dataclass
class HttpResult:
    status: int
    body: dict[str, Any]


def analytics_url(path: str) -> str:
    return env_or_skip("OWANALYTICS_TEST_URL").rstrip("/") + path


def http_json(path: str, token: str | None = None) -> HttpResult:
    headers = {"Accept": "application/json"}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(analytics_url(path), headers=headers, method="GET")
    context = None
    if request.full_url.startswith("https://") and os.environ.get("OWANALYTICS_TEST_VERIFY_TLS") != "1":
        context = ssl._create_unverified_context()
    try:
        with urllib.request.urlopen(request, timeout=10, context=context) as response:
            return HttpResult(response.status, json.loads(response.read().decode("utf-8") or "{}"))
    except urllib.error.HTTPError as exc:
        return HttpResult(exc.code, json.loads(exc.read().decode("utf-8") or "{}"))


def rssi_summary_path(
    timestamp_till: str,
    lookback_hours: str = "1",
    *,
    selected_router_id: str | None = None,
    extra_query: str | None = None,
) -> str:
    selected_router = selected_router_id or router_id()
    encoded = urllib.parse.urlencode(
        {"timestampTill": timestamp_till, "lookbackHours": lookback_hours}
    )
    if extra_query:
        encoded += "&" + extra_query
    return f"/api/v1/devices/{selected_router}/wifi-clients/rssi-summary?{encoded}"


def connect_db(dsn: str):
    try:
        import psycopg

        return psycopg.connect(dsn)
    except ImportError:
        try:
            import psycopg2

            return psycopg2.connect(dsn)
        except ImportError:
            pytest.skip("Install psycopg or psycopg2 to run DB-backed integration tests")


@contextmanager
def db_connection():
    connection = connect_db(env_or_skip("OWANALYTICS_TEST_DB_DSN"))
    try:
        yield connection
        connection.commit()
    finally:
        connection.close()


def cleanup_test_rows(cursor) -> None:
    cursor.execute(
        "delete from timepoints where serialnumber in (%s, %s)",
        (router_id(), "rssi-other-router"),
    )
    cursor.execute(
        "delete from timepoints where boardid in (%s, %s)",
        (board_id(), OTHER_BOARD_ID),
    )
    cursor.execute(
        "delete from boards where id in (%s, %s)",
        (board_id(), OTHER_BOARD_ID),
    )


def seed_board(cursor, *, board: str | None = None, venue: str | None = None, retention: int = 7200) -> None:
    board = board or board_id()
    venue = venue or venue_id()
    now = int(time.time())
    cursor.execute(
        """
        insert into boards (id, name, description, notes, created, modified, venueId, venueName, venueDescription, retention, interval, monitorSubVenues)
        values (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
        """,
        (
            board,
            "RSSI Test Board",
            "Integration test board",
            "[]",
            now,
            now,
            venue,
            "RSSI Test Venue",
            "Integration test venue",
            retention,
            60,
            False,
        ),
    )


def insert_timepoint(
    cursor,
    timestamp: str,
    ssid_data: list[dict[str, Any]] | str,
    *,
    board: str | None = None,
    venue: str | None = None,
    serial: str | None = None,
) -> None:
    board = board or board_id()
    venue = venue or venue_id()
    serial = serial or router_id()
    row_id = f"rssi-{uuid.uuid4().hex}"
    ssid_json = ssid_data if isinstance(ssid_data, str) else json.dumps(ssid_data)
    cursor.execute(
        """
        insert into timepoints (
            id, boardid, timestamp, ap_data, ssid_data, radio_data,
            device_info, serialnumber, resource_data, venueid
        )
        values (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
        """,
        (
            row_id,
            board,
            utc_epoch(timestamp),
            "{}",
            ssid_json,
            "[]",
            "{}",
            serial,
            "{}",
            venue,
        ),
    )


@pytest.fixture
def seeded_board():
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cleanup_test_rows(cursor)
            seed_board(cursor)
    yield
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cleanup_test_rows(cursor)


def test_rssi_summary_aggregates_persisted_rssi_samples(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    start_dt = end_dt - timedelta(hours=1)
    t_a = end_dt - timedelta(minutes=50)
    t_b = end_dt - timedelta(minutes=30)

    ssid_payload_a = [
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [
                {"station": "E2:51:95:ED:0F:28", "rssi": -50},
                {"station": "28:39:26:A1:7C:A5", "rssi": -60},
            ],
        }
    ]
    ssid_payload_b = [
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [
                {"station": "e2:51:95:ed:0f:28", "rssi": -70},
                {"station": "28:39:26:a1:7c:a5", "rssi": -80},
            ],
        }
    ]

    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, format_utc(t_a), ssid_payload_a)
            insert_timepoint(cursor, format_utc(t_b), ssid_payload_b)

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())

    assert result.status == 200
    assert set(result.body) == {"meta", "data"}
    assert result.body["meta"]["requestedWindow"] == {
        "startTime": format_utc(start_dt),
        "endTime": format_utc(end_dt),
    }
    assert result.body["meta"]["observedWindow"] == {
        "startTime": format_utc(t_a),
        "endTime": format_utc(t_b),
    }
    assert result.body["data"]["totalClients"] == 2
    assert result.body["data"]["truncated"] is False
    assert len(result.body["data"]["items"]) == 2

    client_1 = result.body["data"]["items"][0]
    client_2 = result.body["data"]["items"][1]
    assert client_1["mac"] == "28:39:26:a1:7c:a5"
    assert client_2["mac"] == "e2:51:95:ed:0f:28"

    assert client_1["rssi_total_samples"] == 2
    assert client_1["rssi_good_pct"] == 50.0
    assert client_1["rssi_poor_pct"] == 50.0

    assert client_2["rssi_total_samples"] == 2
    assert client_2["rssi_excellent_pct"] == 50.0
    assert client_2["rssi_fair_pct"] == 50.0


def test_rssi_summary_no_samples_returns_empty_success(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    start_dt = end_dt - timedelta(hours=1)

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())

    assert result.status == 200
    assert result.body["meta"]["requestedWindow"] == {
        "startTime": format_utc(start_dt),
        "endTime": format_utc(end_dt),
    }
    assert result.body["meta"]["observedWindow"] == {"startTime": None, "endTime": None}
    assert result.body["data"]["items"] == []
    assert result.body["data"]["totalClients"] == 0
    assert result.body["data"]["truncated"] is False


def test_rssi_summary_ignores_invalid_and_out_of_range_rssi(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    t_a = end_dt - timedelta(minutes=40)

    ssid_payload = [
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [
                {"station": "11:22:33:44:55:66", "rssi": -50},
                {"station": "11:22:33:44:55:66", "rssi": 0},
                {"station": "11:22:33:44:55:66", "rssi": 20},
                {"station": "11:22:33:44:55:66", "rssi": -128},
                {"station": "aa:bb:cc:dd:ee:02", "rssi": 0},
            ],
        }
    ]

    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, format_utc(t_a), ssid_payload)

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())

    assert result.status == 200
    assert result.body["data"]["totalClients"] == 1
    assert len(result.body["data"]["items"]) == 1
    item = result.body["data"]["items"][0]
    assert item["mac"] == "11:22:33:44:55:66"
    assert item["rssi_total_samples"] == 1
    assert item["rssi_excellent_pct"] == 100.0


def test_rssi_summary_ignores_malformed_station_mac_addresses(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    t_a = end_dt - timedelta(minutes=40)

    ssid_payload = [
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [
                {"station": "11:22:33:44:55:66", "rssi": -50},
                {"station": "11::22::33::44::55::66", "rssi": -80},
                {"station": "11-22:33-44:55-66", "rssi": -70},
            ],
        }
    ]

    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, format_utc(t_a), ssid_payload)

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())

    assert result.status == 200
    assert result.body["data"]["totalClients"] == 1
    assert len(result.body["data"]["items"]) == 1
    item = result.body["data"]["items"][0]
    assert item["mac"] == "11:22:33:44:55:66"
    assert item["rssi_total_samples"] == 1
    assert item["rssi_excellent_pct"] == 100.0
    assert item["rssi_fair_pct"] == 0.0
    assert item["rssi_poor_pct"] == 0.0


def test_rssi_summary_gateway_filtering(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    t_a = end_dt - timedelta(minutes=30)

    ssid_payload_requested = [
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [{"station": "11:22:33:44:55:66", "rssi": -50}],
        }
    ]
    ssid_payload_other = [
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [{"station": "11:22:33:44:55:66", "rssi": -90}],
        }
    ]

    with db_connection() as connection:
        with connection.cursor() as cursor:
            # Seed secondary board with its own venue to preserve RouterIdResolver 1-to-1 venue mapping
            seed_board(cursor, board=OTHER_BOARD_ID, venue=OTHER_VENUE_ID)
            # 1. Requested router on requested board & requested venue
            insert_timepoint(cursor, format_utc(t_a), ssid_payload_requested, board=board_id(), venue=venue_id(), serial=router_id())
            # 2. Other router on requested board & requested venue
            insert_timepoint(cursor, format_utc(t_a), ssid_payload_other, board=board_id(), venue=venue_id(), serial="rssi-other-router")
            # 3. Other router on secondary board & secondary venue
            insert_timepoint(cursor, format_utc(t_a), ssid_payload_other, board=OTHER_BOARD_ID, venue=OTHER_VENUE_ID, serial="rssi-other-router")

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())

    assert result.status == 200
    assert result.body["data"]["totalClients"] == 1
    assert len(result.body["data"]["items"]) == 1
    client = result.body["data"]["items"][0]
    assert client["mac"] == "11:22:33:44:55:66"
    assert client["rssi_total_samples"] == 1
    assert client["rssi_excellent_pct"] == 100.0


def test_rssi_summary_malformed_association_entry(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    t_a = end_dt - timedelta(minutes=30)

    # JSON string containing valid association, corrupt non-object element ("corrupt-entry"), malformed object entry, and second valid association
    raw_ssid_json = json.dumps([
        {
            "bssid": "aa:bb:cc:dd:ee:ff",
            "ssid": "main",
            "band": 5,
            "associations": [
                {"station": "11:22:33:44:55:66", "rssi": -50},
                "corrupt-entry",
                {"station": "22:33:44:55:66:77", "rssi": -45, "tx_duration": "invalid"},
                {"station": "aa:bb:cc:dd:ee:ff", "rssi": -60},
            ],
        }
    ])

    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, format_utc(t_a), raw_ssid_json)

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())

    assert result.status == 200
    assert result.body["data"]["totalClients"] == 2
    assert len(result.body["data"]["items"]) == 2
    assert result.body["data"]["items"][0]["mac"] == "11:22:33:44:55:66"
    assert result.body["data"]["items"][1]["mac"] == "aa:bb:cc:dd:ee:ff"


def test_rssi_summary_exceeds_max_samples_from_actual_rows(seeded_board) -> None:
    end_dt = utc_now() - timedelta(seconds=30)
    t_base = end_dt - timedelta(minutes=50)
    base_ts = utc_epoch(format_utc(t_base))

    # Estimated sample count for a 1h window at the board's 60s interval is 60,
    # but the actual persisted row count exceeds the default mcp.max_samples=10,000.
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cursor.execute(
                """
                insert into timepoints (
                    id, boardid, timestamp, ap_data, ssid_data, radio_data,
                    device_info, serialnumber, resource_data, venueid
                )
                select
                    'rssi-exceed-' || s,
                    %s,
                    %s + mod(s, 3000),
                    '{}',
                    '[{"bssid":"aa:bb:cc:dd:ee:ff","ssid":"main","band":5,"associations":[{"station":"11:22:33:44:55:66","rssi":-50}]}]',
                    '[]',
                    '{}',
                    %s,
                    '{}',
                    %s
                from generate_series(1, 10001) as s
                """,
                (board_id(), base_ts, router_id(), venue_id()),
            )

    result = http_json(rssi_summary_path(format_utc(end_dt)), valid_token())
    assert result.status == 400
    assert result.body["error"] == "exceeds_max_samples"
    assert result.body["message"] == "Requested query window exceeds maximum allowed telemetry sample count"


def test_rssi_summary_missing_auth_rejects() -> None:
    result = http_json(rssi_summary_path("2026-07-27T12:00:00Z"))
    assert result.status == 401
    assert result.body["error"] == "unauthorized"


def test_rssi_summary_invalid_query_parameter_rejects() -> None:
    result = http_json(
        rssi_summary_path(format_utc(utc_now() - timedelta(seconds=30)), extra_query="unexpected=true"),
        valid_token(),
    )
    assert result.status == 400
    assert result.body["error"] == "invalid_query_parameter"


def test_rssi_summary_unknown_router_returns_not_found(seeded_board) -> None:
    result = http_json(
        rssi_summary_path(
            format_utc(utc_now() - timedelta(seconds=30)),
            selected_router_id=UNKNOWN_ROUTER_ID,
        ),
        valid_token(),
    )
    assert result.status == 404
    assert result.body["error"] == "not_found"
