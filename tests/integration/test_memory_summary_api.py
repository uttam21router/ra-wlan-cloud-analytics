"""
Integration tests for GET /api/v1/devices/{routerId}/memory-summary.

These tests run against a live OWANALYTICS process. The tests provide a fake
OWSEC HTTP service for bearer-token validation, but OWANALYTICS must be pointed
at that fake service before valid-token tests can pass.

Required environment:
  OWANALYTICS_TEST_URL
      Base URL of the Analytics service, for example http://127.0.0.1:16009.

  OWANALYTICS_TEST_SERVICE_EVENT_COMMAND
      Optional command used to publish one JSON service event to Kafka stdin.
      Example:
        kafka-console-producer --bootstrap-server 127.0.0.1:9092 --topic service_events

  OWANALYTICS_TEST_FAKE_OWSEC_HOST, OWANALYTICS_TEST_FAKE_OWSEC_PORT
      Optional bind address for the fake OWSEC server. Use a fixed port when
      Analytics is configured before pytest starts.

  OWANALYTICS_TEST_FAKE_OWSEC_PUBLIC_URL
      Optional URL advertised to Analytics in the service event. This is useful
      when Analytics runs in Docker and cannot reach pytest's 127.0.0.1.

      If the service event command is omitted, set
      OWANALYTICS_TEST_FAKE_OWSEC_REGISTERED=1 when the running Analytics
      service is already configured to use the fake OWSEC URL.

  OWANALYTICS_TEST_DB_DSN
      PostgreSQL DSN for tests that seed board/timepoint data.

  OWANALYTICS_TEST_ROUTER_ID, OWANALYTICS_TEST_BOARD_ID, OWANALYTICS_TEST_VENUE_ID
      Optional IDs. Defaults match the Markdown integration spec.

The storage-failure test is opt-in because it temporarily changes DB grants:
  OWANALYTICS_TEST_ENABLE_STORAGE_FAILURE=1
  OWANALYTICS_TEST_DB_ADMIN_DSN
  OWANALYTICS_TEST_APP_DB_USER
"""

from __future__ import annotations

import json
import os
import shlex
import ssl
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

import pytest


pytestmark = pytest.mark.integration

DEFAULT_ROUTER_ID = "60cf84f22290"
DEFAULT_BOARD_ID = "board-test-01"
DEFAULT_VENUE_ID = "venue-test-01"
OLD_BOARD_ID = "old-board"
OTHER_BOARD_ID = "other-board"
VALID_TOKEN_PREFIX = "memory-summary-valid"


def env_or_skip(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        pytest.skip(f"{name} is required for memory-summary integration tests")
    return value


def router_id() -> str:
    return os.environ.get("OWANALYTICS_TEST_ROUTER_ID", DEFAULT_ROUTER_ID)


def board_id() -> str:
    return os.environ.get("OWANALYTICS_TEST_BOARD_ID", DEFAULT_BOARD_ID)


def venue_id() -> str:
    return os.environ.get("OWANALYTICS_TEST_VENUE_ID", DEFAULT_VENUE_ID)


def utc_epoch(value: str) -> int:
    return int(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp())


def json_bytes(payload: dict[str, Any]) -> bytes:
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


def user_info_payload() -> dict[str, Any]:
    return {
        "id": "memory-summary-test-user",
        "name": "Memory Summary Test User",
        "description": "",
        "avatar": "",
        "email": "memory-summary-test@example.invalid",
        "validated": True,
        "validationEmail": "",
        "validationDate": 0,
        "creationDate": 0,
        "validationURI": "",
        "changePassword": False,
        "lastLogin": 0,
        "currentLoginURI": "",
        "lastPasswordChange": 0,
        "lastEmailCheck": 0,
        "waitingForEmailCheck": False,
        "locale": "en-US",
        "notes": [],
        "location": "",
        "owner": "",
        "suspended": False,
        "blackListed": False,
        "userRole": "root",
        "userTypeProprietaryInfo": {
            "mobiles": [],
            "mfa": {"enabled": False, "method": ""},
            "authenticatorSecret": "",
        },
        "securityPolicy": "",
        "securityPolicyChange": 0,
        "currentPassword": "",
        "lastPasswords": [],
        "oauthType": "",
        "oauthUserInfo": "",
        "modified": 0,
        "signingUp": "",
    }


def token_payload(token: str) -> dict[str, Any]:
    now = int(time.time())
    return {
        "access_token": token,
        "refresh_token": "refresh-" + token,
        "token_type": "Bearer",
        "expires_in": 3600,
        "idle_timeout": 3600,
        "created": now,
        "username": user_info_payload()["email"],
        "userMustChangePassword": False,
        "errorCode": 0,
        "aclTemplate": {
            "Read": True,
            "ReadWrite": True,
            "ReadWriteCreate": True,
            "Delete": True,
            "PortalLogin": True,
        },
        "lastRefresh": now,
    }


class FakeOwsecState:
    def __init__(self) -> None:
        self.valid_tokens: set[str] = set()
        self.validate_requests: list[str] = []
        self.lock = threading.Lock()

    def issue_token(self, suffix: str) -> str:
        token = f"{VALID_TOKEN_PREFIX}-{suffix}-{time.time_ns()}"
        with self.lock:
            self.valid_tokens.add(token)
        return token

    def validate_count(self, token: str | None = None) -> int:
        with self.lock:
            if token is None:
                return len(self.validate_requests)
            return sum(1 for value in self.validate_requests if value == token)


class FakeOwsecHandler(BaseHTTPRequestHandler):
    server: "FakeOwsecServer"

    def log_message(self, format: str, *args: Any) -> None:
        return

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/v1/validateToken":
            query = urllib.parse.parse_qs(parsed.query)
            token = query.get("token", [""])[0]
            with self.server.state.lock:
                self.server.state.validate_requests.append(token)
                valid = token in self.server.state.valid_tokens
            if not valid:
                self._send_json(401, {"error": "unauthorized"})
                return
            self._send_json(
                200,
                {
                    "tokenInfo": token_payload(token),
                    "userInfo": user_info_payload(),
                },
            )
            return

        if parsed.path == "/api/v1/systemEndpoints":
            self._send_json(200, {"endpoints": []})
            return

        self._send_json(404, {"error": "not_found"})

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/api/v1/oauth2":
            self._send_json(404, {"error": "not_found"})
            return
        token = self.server.state.issue_token("oauth2")
        self._send_json({"access_token": token, "token_type": "Bearer", "expires_in": 3600})

    def _send_json(self, status: int | dict[str, Any], payload: dict[str, Any] | None = None) -> None:
        if payload is None:
            payload = status  # type: ignore[assignment]
            status = 200
        body = json_bytes(payload)
        self.send_response(int(status))
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class FakeOwsecServer(ThreadingHTTPServer):
    def __init__(self, state: FakeOwsecState) -> None:
        host = os.environ.get("OWANALYTICS_TEST_FAKE_OWSEC_HOST", "127.0.0.1")
        port = int(os.environ.get("OWANALYTICS_TEST_FAKE_OWSEC_PORT", "0"))
        super().__init__((host, port), FakeOwsecHandler)
        self.state = state


@dataclass
class FakeOwsec:
    url: str
    state: FakeOwsecState
    server: FakeOwsecServer
    thread: threading.Thread

    def issue_token(self, suffix: str) -> str:
        return self.state.issue_token(suffix)

    def validate_count(self, token: str | None = None) -> int:
        return self.state.validate_count(token)

    def stop(self) -> None:
        self.server.shutdown()
        self.thread.join(timeout=5)
        self.server.server_close()


@pytest.fixture(scope="session")
def fake_owsec() -> FakeOwsec:
    state = FakeOwsecState()
    server = FakeOwsecServer(state)
    host, port = server.server_address
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = os.environ.get("OWANALYTICS_TEST_FAKE_OWSEC_PUBLIC_URL", f"http://{host}:{port}")
    fake = FakeOwsec(url=url, state=state, server=server, thread=thread)
    register_fake_owsec(fake.url)
    yield fake
    fake.stop()


def register_fake_owsec(private_url: str) -> None:
    event = {
        "event": "join",
        "id": 990001,
        "type": "owsec",
        "publicEndPoint": private_url,
        "privateEndPoint": private_url,
        "key": "fake-owsec-test-key",
        "version": "test",
    }
    command = os.environ.get("OWANALYTICS_TEST_SERVICE_EVENT_COMMAND")
    if command:
        subprocess.run(
            shlex.split(command),
            input=json.dumps(event) + "\n",
            text=True,
            check=True,
            timeout=10,
        )
        time.sleep(float(os.environ.get("OWANALYTICS_TEST_SERVICE_EVENT_WAIT", "1")))
        return

    if os.environ.get("OWANALYTICS_TEST_FAKE_OWSEC_REGISTERED") == "1":
        return

    pytest.skip(
        "Set OWANALYTICS_TEST_SERVICE_EVENT_COMMAND so tests can register fake "
        "OWSEC, or set OWANALYTICS_TEST_FAKE_OWSEC_REGISTERED=1 if Analytics is "
        "already configured to use the fake OWSEC service."
    )


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


def memory_summary_path(
    timestamp_till: str = "2026-07-29T12:00:00Z",
    lookback_hours: str = "2",
    extra_query: str | None = None,
) -> str:
    query = {
        "timestampTill": timestamp_till,
        "lookbackHours": lookback_hours,
    }
    encoded = urllib.parse.urlencode(query)
    if extra_query:
        encoded += "&" + extra_query
    return f"/api/v1/devices/{router_id()}/memory-summary?{encoded}"


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


def sql_identifier(value: str) -> str:
    if not value or not all(c.isalnum() or c == "_" for c in value):
        pytest.fail(f"Unsafe SQL identifier in OWANALYTICS_TEST_APP_DB_USER: {value!r}")
    return value


@contextmanager
def db_connection():
    dsn = env_or_skip("OWANALYTICS_TEST_DB_DSN")
    connection = connect_db(dsn)
    try:
        yield connection
        connection.commit()
    finally:
        connection.close()


def cleanup_test_rows(cursor) -> None:
    boards = (board_id(), OLD_BOARD_ID, OTHER_BOARD_ID)
    cursor.execute("delete from timepoints where serialnumber in (%s, %s)", (router_id(), "other-router"))
    cursor.execute("delete from timepoints where boardid in (%s, %s, %s)", boards)
    cursor.execute("delete from board_venues where board_id in (%s, %s, %s)", boards)
    cursor.execute("delete from board_venues where venue_id = %s", (venue_id(),))
    cursor.execute("delete from boards where id in (%s, %s, %s)", boards)


def seed_board(cursor, retention: int = 7200, board: str | None = None, venue: str | None = None) -> None:
    board = board or board_id()
    venue = venue or venue_id()
    venue_list = [
        {
            "id": venue,
            "name": "Memory Summary Test Venue",
            "description": "Integration test venue",
            "retention": retention,
            "interval": 60,
            "monitorSubVenues": False,
        }
    ]
    now = int(time.time())
    cursor.execute(
        """
        insert into boards (id, name, description, notes, created, modified, venuelist)
        values (%s, %s, %s, %s, %s, %s, %s)
        """,
        (
            board,
            "Memory Summary Test Board",
            "Integration test board",
            "[]",
            now,
            now,
            json.dumps(venue_list),
        ),
    )
    cursor.execute(
        "insert into board_venues (board_id, venue_id) values (%s, %s)",
        (board, venue),
    )


def insert_timepoint(
    cursor,
    timestamp: str,
    resource_data: dict[str, Any],
    *,
    board: str | None = None,
    serial: str | None = None,
    suffix: str = "",
) -> None:
    board = board or board_id()
    serial = serial or router_id()
    row_id = f"mem-int-{board}-{serial}-{utc_epoch(timestamp)}-{suffix}".replace(":", "-")
    cursor.execute(
        """
        insert into timepoints (
            id, boardid, timestamp, ap_data, ssid_data, radio_data,
            device_info, serialnumber, resource_data
        )
        values (%s, %s, %s, %s, %s, %s, %s, %s, %s)
        """,
        (
            row_id,
            board,
            utc_epoch(timestamp),
            "{}",
            "[]",
            "[]",
            "{}",
            serial,
            json.dumps(resource_data),
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


def assert_empty_summary(body: dict[str, Any]) -> None:
    assert body["meta"]["requestedWindow"] == {
        "startTime": "2026-07-29T10:00:00Z",
        "endTime": "2026-07-29T12:00:00Z",
    }
    assert body["meta"]["observedWindow"] == {"startTime": None, "endTime": None}
    assert body["data"] == {
        "min_memfree": None,
        "max_memfree": None,
        "avg_memfree": None,
        "latest_memfree": None,
    }


def test_memory_summary_aggregates_persisted_samples(fake_owsec: FakeOwsec, seeded_board) -> None:
    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, "2026-07-29T10:05:00Z", {"memory_free": 200000, "memory_total": 512000}, suffix="a")
            insert_timepoint(cursor, "2026-07-29T10:30:00Z", {"memory_free": 250000, "memory_total": 512000}, suffix="b")
            insert_timepoint(cursor, "2026-07-29T11:55:00Z", {"memory_free": 300000, "memory_total": 512000}, suffix="c")

    token = fake_owsec.issue_token("happy-path")
    result = http_json(memory_summary_path(), token)

    assert result.status == 200
    assert set(result.body) == {"data", "meta"}
    assert result.body["meta"]["requestedWindow"] == {
        "startTime": "2026-07-29T10:00:00Z",
        "endTime": "2026-07-29T12:00:00Z",
    }
    assert result.body["meta"]["observedWindow"] == {
        "startTime": "2026-07-29T10:05:00Z",
        "endTime": "2026-07-29T11:55:00Z",
    }
    assert result.body["data"] == {
        "min_memfree": 200000,
        "max_memfree": 300000,
        "avg_memfree": 250000,
        "latest_memfree": 300000,
    }


def test_memory_summary_applies_half_open_window_and_filters_board_and_serial(
    fake_owsec: FakeOwsec, seeded_board
) -> None:
    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, "2026-07-29T09:59:59Z", {"memory_free": 111111, "memory_total": 512000}, suffix="before")
            insert_timepoint(cursor, "2026-07-29T10:00:00Z", {"memory_free": 200000, "memory_total": 512000}, suffix="start")
            insert_timepoint(cursor, "2026-07-29T11:59:59Z", {"memory_free": 300000, "memory_total": 512000}, suffix="inside")
            insert_timepoint(cursor, "2026-07-29T12:00:00Z", {"memory_free": 999999, "memory_total": 999999}, suffix="end")
            insert_timepoint(
                cursor,
                "2026-07-29T10:30:00Z",
                {"memory_free": 1, "memory_total": 512000},
                serial="other-router",
                suffix="other-router",
            )
            insert_timepoint(
                cursor,
                "2026-07-29T10:30:00Z",
                {"memory_free": 2, "memory_total": 512000},
                board=OTHER_BOARD_ID,
                suffix="other-board",
            )

    result = http_json(memory_summary_path(), fake_owsec.issue_token("filters"))

    assert result.status == 200
    assert result.body["data"] == {
        "min_memfree": 200000,
        "max_memfree": 300000,
        "avg_memfree": 250000,
        "latest_memfree": 300000,
    }
    assert result.body["meta"]["observedWindow"] == {
        "startTime": "2026-07-29T10:00:00Z",
        "endTime": "2026-07-29T11:59:59Z",
    }


def test_memory_summary_no_samples_returns_empty_success(fake_owsec: FakeOwsec, seeded_board) -> None:
    result = http_json(memory_summary_path(), fake_owsec.issue_token("empty"))

    assert result.status == 200
    assert_empty_summary(result.body)


def test_memory_summary_ignores_invalid_resource_rows(fake_owsec: FakeOwsec, seeded_board) -> None:
    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(cursor, "2026-07-29T10:05:00Z", {}, suffix="empty")
            insert_timepoint(cursor, "2026-07-29T10:10:00Z", {"memory_total": 512000}, suffix="missing-free")
            insert_timepoint(cursor, "2026-07-29T10:15:00Z", {"memory_free": 700000, "memory_total": 512000}, suffix="corrupt")
            insert_timepoint(cursor, "2026-07-29T10:20:00Z", {"memory_free": 200000, "memory_total": 512000}, suffix="valid-a")
            insert_timepoint(cursor, "2026-07-29T10:25:00Z", {"memory_free": 300000}, suffix="valid-b")

    result = http_json(memory_summary_path(), fake_owsec.issue_token("invalid-resource"))

    assert result.status == 200
    assert result.body["data"] == {
        "min_memfree": 200000,
        "max_memfree": 300000,
        "avg_memfree": 250000,
        "latest_memfree": 300000,
    }
    assert result.body["meta"]["observedWindow"] == {
        "startTime": "2026-07-29T10:20:00Z",
        "endTime": "2026-07-29T10:25:00Z",
    }


def test_memory_summary_missing_auth_rejects_before_fake_owsec_or_query_validation(
    fake_owsec: FakeOwsec,
) -> None:
    before = fake_owsec.validate_count()
    result = http_json(memory_summary_path(timestamp_till="not-a-timestamp", lookback_hours="bad"))

    assert result.status == 401
    assert result.body["error"] == "unauthorized"
    assert fake_owsec.validate_count() == before


def test_memory_summary_invalid_query_rejects_after_auth(fake_owsec: FakeOwsec) -> None:
    token = fake_owsec.issue_token("invalid-query")
    result = http_json(memory_summary_path(extra_query="unexpected=true"), token)

    assert fake_owsec.validate_count(token) == 1
    assert result.status == 400
    assert result.body["error"] == "invalid_query_parameter"


def test_memory_summary_current_router_ownership_controls_board_data(
    fake_owsec: FakeOwsec, seeded_board
) -> None:
    with db_connection() as connection:
        with connection.cursor() as cursor:
            insert_timepoint(
                cursor,
                "2026-07-29T10:15:00Z",
                {"memory_free": 999999, "memory_total": 999999},
                board=OLD_BOARD_ID,
                suffix="old",
            )
            insert_timepoint(cursor, "2026-07-29T10:20:00Z", {"memory_free": 200000, "memory_total": 512000}, suffix="current-a")
            insert_timepoint(cursor, "2026-07-29T10:25:00Z", {"memory_free": 300000, "memory_total": 512000}, suffix="current-b")

    result = http_json(memory_summary_path(), fake_owsec.issue_token("current-owner"))

    assert result.status == 200
    assert result.body["data"] == {
        "min_memfree": 200000,
        "max_memfree": 300000,
        "avg_memfree": 250000,
        "latest_memfree": 300000,
    }


def test_memory_summary_retention_is_enforced_from_resolved_venue(fake_owsec: FakeOwsec) -> None:
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cleanup_test_rows(cursor)
            seed_board(cursor, retention=3600)

    try:
        result = http_json(memory_summary_path(), fake_owsec.issue_token("retention"))
        assert result.status == 400
        assert result.body["error"] == "invalid_lookback_hours"
    finally:
        with db_connection() as connection:
            with connection.cursor() as cursor:
                cleanup_test_rows(cursor)


def test_memory_summary_without_analytics_board_mapping_returns_not_found(fake_owsec: FakeOwsec) -> None:
    with db_connection() as connection:
        with connection.cursor() as cursor:
            cleanup_test_rows(cursor)

    result = http_json(memory_summary_path(), fake_owsec.issue_token("no-board"))

    assert result.status == 404
    assert result.body["error"] == "not_found"


def test_memory_summary_timepoint_storage_failure_returns_memory_specific_error(
    fake_owsec: FakeOwsec,
) -> None:
    if os.environ.get("OWANALYTICS_TEST_ENABLE_STORAGE_FAILURE") != "1":
        pytest.skip("Set OWANALYTICS_TEST_ENABLE_STORAGE_FAILURE=1 to run this DB grant test")

    admin_dsn = env_or_skip("OWANALYTICS_TEST_DB_ADMIN_DSN")
    app_user = sql_identifier(env_or_skip("OWANALYTICS_TEST_APP_DB_USER"))

    with db_connection() as connection:
        with connection.cursor() as cursor:
            cleanup_test_rows(cursor)
            seed_board(cursor)

    admin = connect_db(admin_dsn)
    try:
        with admin.cursor() as cursor:
            cursor.execute(f"revoke select on table timepoints from {app_user}")
        admin.commit()

        result = http_json(memory_summary_path(), fake_owsec.issue_token("storage-failure"))
        assert result.status == 500
        assert result.body["error"] == "memory_summary_query_failed"
        assert result.body["message"] == "Unable to retrieve gateway memory history"
    finally:
        with admin.cursor() as cursor:
            cursor.execute(f"grant select on table timepoints to {app_user}")
        admin.commit()
        admin.close()
        with db_connection() as connection:
            with connection.cursor() as cursor:
                cleanup_test_rows(cursor)
