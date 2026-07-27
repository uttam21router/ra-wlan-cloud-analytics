# Analytics APIs for MCP Tools

This document defines the request format, response format, and implementation logic for the following MCP tools in the `ra-wlan-cloud-analytics` service:

- `get_gateway_free_memory`
- `get_gateway_wifi_temp`
- `get_device_bandwidth_consumption`
- `get_device_rssi_quality`
- `get_gateway_offline_count`

The API contracts match the MCP tool names and response fields from the provided CSV.

---

# Common Input Parameters

Each MCP tool receives:

```text
router_id: str
timestamp_till: str
lookback_hours: int
```

`router_id` is the gateway serial number value from the CSV. Analytics APIs expose the same value as the path variable `routerId`.

```text
MCP input:              router_id
Analytics path variable: routerId
routerId = router_id
Internal storage field: serialNumber
serialNumber = routerId
```

The Analytics API should calculate the requested time range as:

```text
end_time = parse(timestamp_till)
start_time = end_time - (lookback_hours * 3600)
```

Example:

```text
timestamp_till = 2026-07-27T12:00:00Z
lookback_hours = 24

start_time = 2026-07-26T12:00:00Z
end_time   = 2026-07-27T12:00:00Z
```

Recommended query parameters:

```http
?timestampTill=2026-07-27T12:00:00Z
&lookbackHours=24
```

These are `GET` APIs, so no request body is required.

### Time Conversion and Validation

The public MCP-facing parameters use ISO-8601/RFC3339 time, but the existing Analytics API style uses integer `fromDate` and `endDate` timestamps. Handlers should convert the request as:

```text
timestampTill: RFC3339 UTC string, for example 2026-07-27T12:00:00Z
endTime:       Unix epoch seconds parsed from timestampTill
startTime:     endTime - (lookbackHours * 3600)
```

Validation rules:

```text
timestampTill must parse as a valid timestamp.
lookbackHours must be greater than 0.
lookbackHours should have a bounded maximum, for example 24 * 31.
startTime must be less than endTime.
```

Return `400 Bad Request` for invalid timestamps, unsupported timezones, non-positive `lookbackHours`, or values above the configured maximum. Internally, query `timepoints.timestamp` and `wificlienthistory.timestamp` using epoch seconds.

### Current Storage Model

The current service does not store normalized tables named `device_timepoints`, `radio_timepoints`, or `wifi_client_history`.

Current tables are:

```text
timepoints
  boardId
  timestamp
  ap_data      JSON string
  ssid_data    JSON string
  radio_data   JSON string
  device_info  JSON string
  serialNumber

wificlienthistory
  timestamp
  station_id
  bssid
  ssid
  rssi
  rx_bytes
  tx_bytes
  venue_id
  ...
```

Gateway-scoped APIs should accept only `routerId` publicly. The handler must map `routerId` to the existing internal `serialNumber` field and resolve the internal `boardId` before loading `TimePointDB` records. Do not rename the existing `timepoints.serialNumber` field or its indexes to `routerId`.

### RouterId Resolution

The MCP tool cannot pass `boardId`, so Analytics must resolve it from `routerId`, which is the gateway serial number.

Resolution flow:

```text
1. Validate routerId.
2. Call OWPROV inventory for the serial number with status-aware handling:
     GET /api/v1/inventory/{routerId}
3. Read inventoryTag.venue as venueId.
4. Find the Analytics board that currently owns routerId.
5. Use that board's BoardInfo.info.id as resolvedBoardId.
6. Query Analytics storage with resolvedBoardId and serialNumber = routerId.
```

Implementation notes:

```text
OWPROV source:
  /api/v1/inventory/{routerId}

OWPROV response field:
  InventoryTag.venue

Analytics local source:
  BoardsDB records
  BoardInfo.venueList[].id
  BoardInfo.venueList[].monitorSubVenues
```

Board ownership resolution must account for child venues. Do not only check whether `inventoryTag.venue` is directly present in `BoardInfo.venueList`.

Preferred ownership algorithm:

```text
For each local BoardInfo record:
  for each VenueInfo in BoardInfo.venueList:
    call SDK::Prov::Venue::GetDevices(
      client,
      VenueInfo.id,
      VenueInfo.monitorSubVenues,
      venueDeviceList,
      venueExists)

    if routerId is present in venueDeviceList.devices:
      add BoardInfo.info.id as a candidate board

If exactly one candidate exists:
  resolvedBoardId = candidate board
```

This mirrors the existing watcher behavior, where board devices are fetched from OWPROV with the board venue's `monitorSubVenues` setting.

Alternative implementation:

```text
Expose a current routerId -> boardId map from VenueCoordinator.
The map must be maintained from the same device lists used by VenueWatcher.
The resolver may use this map instead of scanning OWPROV for every request.
```

Failure handling:

```text
OWPROV inventory not found              -> 404 Not Found
Inventory exists but venue is empty     -> 404 Not Found
No Analytics board configured for venue -> 404 Not Found
Multiple matching boards                -> 409 Conflict unless deterministic ownership exists
OWPROV unavailable or invalid response  -> 502 Bad Gateway
```

The resolver must return a status-bearing result, not a boolean, so handlers can map each failure to the correct HTTP response.

```cpp
enum class RouterIdResolutionStatus {
    Success,
    InvalidRouterId,
    InventoryNotFound,
    EmptyVenue,
    BoardNotConfigured,
    MultipleBoards,
    OwprovUnavailable,
    OwprovInvalidResponse
};

struct RouterIdResolutionResult {
    RouterIdResolutionStatus status = RouterIdResolutionStatus::OwprovUnavailable;
    std::string routerId;
    std::string venueId;
    std::string resolvedBoardId;
    std::string message;
};
```

Status mapping:

```text
Success               -> continue request
InvalidRouterId   -> 400 Bad Request
InventoryNotFound     -> 404 Not Found
EmptyVenue            -> 404 Not Found
BoardNotConfigured    -> 404 Not Found
MultipleBoards        -> 409 Conflict
OwprovUnavailable     -> 502 Bad Gateway
OwprovInvalidResponse -> 502 Bad Gateway
```

The existing `SDK::Prov::Device::Get` helper returns only `bool` and does not expose the OWPROV HTTP status. Do not use that bool-only helper when the handler must distinguish `404 Not Found` from OWPROV connectivity or parsing failures. Use one of these instead:

```text
Option A:
  Add a status-aware SDK helper, for example:
    SDK::Prov::Device::GetWithStatus(...)

Option B:
  Call OpenAPIRequestGet(uSERVICE_PROVISIONING, "/api/v1/inventory/{routerId}", ...)
  directly from ResolveRouterIdContext and inspect the HTTP response status.
```

Handlers may cache `routerId -> venueId -> boardId` for a short TTL, but must refresh or invalidate the cache when OWPROV reports a different venue.

---

# 1. `get_gateway_free_memory`

## MCP Tool Signature

```text
get_gateway_free_memory(
    router_id: str,
    timestamp_till: str,
    lookback_hours: int
)
```

## Recommended API

```http
GET /api/v1/devices/{routerId}/memory-summary
    ?timestampTill=2026-07-27T12:00:00Z
    &lookbackHours=24
```

## Example Request

```http
GET /api/v1/devices/60cf84f22290/memory-summary?timestampTill=2026-07-27T12:00:00Z&lookbackHours=24
Authorization: Bearer <token>
```

## Request Body

```text
None
```

## Response

```json
{
  "min_memfree": 211374,
  "max_memfree": 215050,
  "avg_memfree": 212074.36
}
```

## API Logic

The gateway state payload contains:

```text
unit.memory.free
unit.memory.total
unit.memory.cached
unit.memory.buffered
```

The current Analytics implementation calculates memory usage percentage from `free` and `total`, but does not preserve all raw memory values as separate historical fields.

### Required Model Change

Add raw memory fields to the device time-point model and persist them with each `timepoints` record:

```cpp
struct DeviceResourceTimePoint {
    uint64_t memory_free = 0;
    uint64_t memory_total = 0;
    uint64_t memory_cached = 0;
    uint64_t memory_buffered = 0;
};

struct DeviceTimePoint {
    ...
    DeviceResourceTimePoint resource_data;
};
```

Required code changes:

```text
src/RESTObjects/RESTAPI_AnalyticsObjects.h
  Add DeviceResourceTimePoint.
  Add DeviceTimePoint::resource_data.

src/RESTObjects/RESTAPI_AnalyticsObjects.cpp
  Add DeviceResourceTimePoint to_json/from_json.
  Add resource_data to DeviceTimePoint to_json/from_json.

src/storage/storage_timepoints.h
  Extend TimePointDBRecordType with a resource_data JSON string field.

src/storage/storage_timepoints.cpp
  Add ORM field resource_data.
  Update Convert(record -> DeviceTimePoint).
  Update Convert(DeviceTimePoint -> record).
  Add an Upgrade migration for existing DBs.

src/APStats.cpp
  Populate DTP.resource_data from unit.memory during state ingestion.
```

Existing rows will not have `resource_data`; treat those rows as missing memory samples rather than zero free memory.

### Ingestion Logic

```cpp
AnalyticsObjects::DeviceResourceTimePoint resource;
GetJSON("free", memory, resource.memory_free, uint64_t{0});
GetJSON("total", memory, resource.memory_total, uint64_t{0});
GetJSON("cached", memory, resource.memory_cached, uint64_t{0});
GetJSON("buffered", memory, resource.memory_buffered, uint64_t{0});
DTP.resource_data = resource;
```

### Aggregation Logic

Use the current `timepoints` table plus parsed JSON fields:

```text
Load TimePointDB records where:
  boardId == resolvedBoardId
  stored serialNumber == request routerId
  timestamp >= startTime
  timestamp <= endTime

For each record:
  read record.resource_data.memory_free
  ignore missing resource_data
  ignore memory_free when memory_total is 0 and the sample came from a missing memory block

Return:
  min_memfree = min(memory_free samples)
  max_memfree = max(memory_free samples)
  avg_memfree = sum(memory_free samples) / sample_count
```

Do not query `device_timepoints.memory_free` unless a separate normalized table and migration are introduced.

### Handler Flow

```text
Validate routerId
    ↓
Call ResolveRouterIdContext(client, routerId)
    ↓
If status is not Success, map RouterIdResolutionStatus to HTTP response
    ↓
Use result.venueId and result.resolvedBoardId
    ↓
Parse timestampTill
    ↓
Calculate start_time
    ↓
Query timepoints and read resource_data.memory_free samples
    ↓
Calculate min, max and average
    ↓
Return MCP response shape
```

### Empty Result

```json
{
  "min_memfree": null,
  "max_memfree": null,
  "avg_memfree": null
}
```

Use HTTP `200 OK` when the query succeeds but no data exists.

---

# 2. `get_gateway_wifi_temp`

## MCP Tool Signature

```text
get_gateway_wifi_temp(
    router_id: str,
    timestamp_till: str,
    lookback_hours: int
)
```

## Recommended API

```http
GET /api/v1/devices/{routerId}/radio-temperature-summary
    ?timestampTill=2026-07-27T12:00:00Z
    &lookbackHours=24
```

## Example Request

```http
GET /api/v1/devices/60cf84f22290/radio-temperature-summary?timestampTill=2026-07-27T12:00:00Z&lookbackHours=24
Authorization: Bearer <token>
```

## Request Body

```text
None
```

## Response

```json
{
  "min_wifi_temp_2.4G": 62,
  "max_wifi_temp_2.4G": 70,
  "avg_wifi_temp_2.4G": 66.64,
  "min_wifi_temp_5G": 56,
  "max_wifi_temp_5G": 65,
  "avg_wifi_temp_5G": 60.38
}
```

## API Logic

Analytics already stores:

```text
radios[].band
radios[].temperature
```

The current radio-band mapping is:

```text
2G → 2
5G → 5
6G → 6
```

Response mapping:

```text
band = 2 → fields ending in _2.4G
band = 5 → fields ending in _5G
```

### Aggregation Logic

Use the current `timepoints.radio_data` JSON array:

```text
Load TimePointDB records where:
  boardId == resolvedBoardId
  stored serialNumber == request routerId
  timestamp >= startTime
  timestamp <= endTime

For each record:
  parse radio_data
  for each radio in radio_data:
    if radio.band is 2 or 5:
      add radio.temperature to that band's sample list

For each band:
  min_temperature = min(samples)
  max_temperature = max(samples)
  avg_temperature = sum(samples) / sample_count
```

Do not query `radio_timepoints` unless a separate normalized table and migration are introduced.

### Handler Logic

```cpp
if (radio.band == 2) {
    response.min_wifi_temp_2_4G = summary.min;
    response.max_wifi_temp_2_4G = summary.max;
    response.avg_wifi_temp_2_4G = summary.avg;
}

if (radio.band == 5) {
    response.min_wifi_temp_5G = summary.min;
    response.max_wifi_temp_5G = summary.max;
    response.avg_wifi_temp_5G = summary.avg;
}
```

### Missing Band Example

```json
{
  "min_wifi_temp_2.4G": 62,
  "max_wifi_temp_2.4G": 70,
  "avg_wifi_temp_2.4G": 66.64,
  "min_wifi_temp_5G": null,
  "max_wifi_temp_5G": null,
  "avg_wifi_temp_5G": null
}
```

---

# 3. `get_device_bandwidth_consumption`

## MCP Tool Signature

```text
get_device_bandwidth_consumption(
    router_id: str,
    timestamp_till: str,
    lookback_hours: int
)
```

## Recommended API

```http
GET /api/v1/devices/{routerId}/wifi-clients/usage-summary
    ?timestampTill=2026-07-27T12:00:00Z
    &lookbackHours=24
```

## Example Request

```http
GET /api/v1/devices/60cf84f22290/wifi-clients/usage-summary?timestampTill=2026-07-27T12:00:00Z&lookbackHours=24
Authorization: Bearer <token>
```

## Request Body

```text
None
```

## Response

```json
[
  {
    "mac": "e2:51:95:ed:0f:28",
    "data_consume_rx": "851.9 Mb",
    "data_consume_tx": "30.81 Mb",
    "total_data_usage": "882.71 Mb"
  },
  {
    "mac": "28:39:26:a1:7c:a5",
    "data_consume_rx": "240.57 Mb",
    "data_consume_tx": "131.89 Mb",
    "total_data_usage": "372.46 Mb"
  }
]
```

## API Logic

For gateway-scoped results, use `timepoints.ssid_data[].associations[]` because `wificlienthistory` currently does not store the gateway `serialNumber`.

`timepoints.ssid_data[].associations[]` already stores per-client:

```text
station MAC
RX bytes
TX bytes
packet counters
connected duration
BSSID
SSID
radio information
```

Alternative implementation:

```text
If wificlienthistory is preferred for performance, first add the gateway `serialNumber` to:
  AnalyticsObjects::WifiClientHistory
  WifiClientHistoryDBRecordType
  storage_wificlients.cpp fields and converters
  APStats.cpp ingestion
  DB upgrade migration
```

The values are cumulative counters, so they must not be summed directly.

### Correct Calculation

```text
baseline = last sample before start_time for the same client, if available

first in-window delta:
  if baseline exists:
    resetSafeDelta(first.rx_bytes, baseline.rx_bytes)
  else:
    0

subsequent deltas:
  resetSafeDelta(current.rx_bytes, previous.rx_bytes)

data_consume_rx = SUM(reset-safe rx_bytes deltas)
data_consume_tx = SUM(reset-safe tx_bytes deltas)
total_data_usage = data_consume_rx + data_consume_tx
```

### Reset-Safe Delta Logic

```cpp
uint64_t resetSafeDelta(uint64_t current, uint64_t previous) {
    if (current >= previous) {
        return current - previous;
    }

    return current;
}
```

This handles:

```text
client reconnection
counter reset
gateway reboot
counter rollover
```

Without the pre-window baseline, the first sample inside the requested window can include traffic that occurred before `start_time`. Do not add the first in-window cumulative counter directly unless it is known to be a fresh association/reset.

### Incorrect Calculation

```sql
SUM(rx_bytes)
SUM(tx_bytes)
```

This would overcount cumulative values.

### Grouping

```text
GROUP BY station_id
```

A client moving between BSSIDs should remain one client unless a per-BSSID result is explicitly required.

### Unit Conversion

The MCP output expects strings such as:

```text
851.9 Mb
30.81 Mb
```

Recommended conversion:

```cpp
double rxMb = static_cast<double>(rxBytes) * 8.0 / 1000000.0;
double txMb = static_cast<double>(txBytes) * 8.0 / 1000000.0;
```

`Mb` means megabits. If the implementation divides bytes by `1024 * 1024`, label the result as `MiB` or `MB` instead.

Formatting:

```cpp
fmt::format("{:.2f} Mb", value);
```

### Query Flow

```text
Resolve boardId from routerId
    ↓
Load TimePointDB records for resolvedBoardId and stored serialNumber == request routerId
    ↓
Include records from startTime through endTime
    ↓
Also load the latest pre-window association sample for each station MAC
    ↓
Parse ssid_data associations
    ↓
Group by station MAC and sort by timestamp
    ↓
Calculate reset-safe RX/TX deltas
    ↓
Sum values by MAC
    ↓
Convert bytes to megabits
    ↓
Return array
```

---

# 4. `get_device_rssi_quality`

## MCP Tool Signature

```text
get_device_rssi_quality(
    router_id: str,
    timestamp_till: str,
    lookback_hours: int
)
```

## Recommended API

```http
GET /api/v1/devices/{routerId}/wifi-clients/rssi-summary
    ?timestampTill=2026-07-27T12:00:00Z
    &lookbackHours=24
```

## Example Request

```http
GET /api/v1/devices/60cf84f22290/wifi-clients/rssi-summary?timestampTill=2026-07-27T12:00:00Z&lookbackHours=24
Authorization: Bearer <token>
```

## Request Body

```text
None
```

## Response

```json
[
  {
    "mac": "e2:51:95:ed:0f:28",
    "rssi_excellent_pct": 41.67,
    "rssi_good_pct": 50.0,
    "rssi_fair_pct": 1.67,
    "rssi_poor_pct": 6.67,
    "rssi_total_samples": 60
  },
  {
    "mac": "28:39:26:a1:7c:a5",
    "rssi_excellent_pct": 92.73,
    "rssi_good_pct": 7.27,
    "rssi_fair_pct": 0.0,
    "rssi_poor_pct": 0.0,
    "rssi_total_samples": 110
  }
]
```

## API Logic

For gateway-scoped results, read RSSI from `timepoints.ssid_data[].associations[]`. `wificlienthistory` also stores RSSI, but it cannot be filtered by gateway `serialNumber` unless that model is extended as described in the bandwidth section.

### RSSI Thresholds

```text
Excellent: RSSI >= -55
Good:      -67 <= RSSI < -55
Fair:      -75 <= RSSI < -67
Poor:      RSSI < -75
```

### Invalid Samples

Ignore:

```text
RSSI = 0
RSSI > 0
RSSI < -127
NULL
```

### Per-Client Calculation

```text
excellent_count = COUNT(rssi >= -55)

good_count =
    COUNT(rssi >= -67 AND rssi < -55)

fair_count =
    COUNT(rssi >= -75 AND rssi < -67)

poor_count =
    COUNT(rssi < -75)

total_samples =
    excellent_count +
    good_count +
    fair_count +
    poor_count
```

Percentages:

```text
excellent_pct = excellent_count × 100 / total_samples
good_pct      = good_count × 100 / total_samples
fair_pct      = fair_count × 100 / total_samples
poor_pct      = poor_count × 100 / total_samples
```

Round percentages to two decimal places.

### Current Storage Aggregation Logic

```text
Load TimePointDB records where:
  boardId == resolvedBoardId
  stored serialNumber == request routerId
  timestamp >= startTime
  timestamp <= endTime

For each record:
  parse ssid_data
  for each association:
    station_id = normalized association.station MAC
    rssi = association.rssi
    ignore invalid RSSI samples
    increment the station_id bucket for excellent/good/fair/poor

For each station_id:
  total_samples = excellent + good + fair + poor
  calculate percentages from total_samples
```

Do not query `wifi_client_history.board_id`, `wifi_client_history.serialNumber`, or `wifi_client_history.created`; those fields do not exist in the current schema.

The API should return the final percentages directly. The MCP server should not need to perform additional RSSI summarisation.

---

# 5. `get_gateway_offline_count`

## MCP Tool Signature

```text
get_gateway_offline_count(
    router_id: str,
    timestamp_till: str,
    lookback_hours: int
)
```

## Recommended API

```http
GET /api/v1/devices/{routerId}/availability-summary
    ?timestampTill=2026-07-27T12:00:00Z
    &lookbackHours=24
```

## Example Request

```http
GET /api/v1/devices/60cf84f22290/availability-summary?timestampTill=2026-07-27T12:00:00Z&lookbackHours=24
Authorization: Bearer <token>
```

## Request Body

```text
None
```

## Response

```json
{
  "gw_uuid": "60cf84f22290",
  "fetch_status": "success",
  "offline_count": 6
}
```

## API Logic

This API can use gateway events from the existing `connection` topic.

Analytics currently handles gateway-level events:

```text
ping
disconnection
capabilities
```

The current code updates:

```text
connected
lastConnection
lastDisconnection
lastPing
lastContact
```

To calculate historical offline counts, transitions must be persisted.

### Storage Table

```text
device_availability_events
--------------------------
id
board_id
serialNumber
event_type
event_time
reason
connection_ip
session_id
event_id
metadata
```

### Required Storage Implementation

Add a dedicated storage class for availability events:

```text
src/storage/storage_device_availability_events.h
src/storage/storage_device_availability_events.cpp
```

Required ORM fields and indexes:

```text
Fields:
  id              TEXT primary id
  board_id        TEXT
  serialNumber    TEXT
  event_type      TEXT
  event_time      BIGINT
  reason          TEXT
  connection_ip   TEXT
  session_id      TEXT
  event_id        TEXT
  metadata        TEXT

Indexes:
  availability_serial_time_index:
    serialNumber ASC
    event_time ASC

  availability_board_serial_time_index:
    board_id ASC
    serialNumber ASC
    event_time ASC

  availability_event_id_index:
    event_id ASC
```

Add a `DeviceAvailabilityEvent` REST/storage object with `to_json` and `from_json` support in:

```text
src/RESTObjects/RESTAPI_AnalyticsObjects.h
src/RESTObjects/RESTAPI_AnalyticsObjects.cpp
```

Update `StorageService`:

```text
src/StorageService.h
  include storage/storage_device_availability_events.h
  add DeviceAvailabilityEventsDB accessor
  add std::unique_ptr<DeviceAvailabilityEventsDB>

src/StorageService.cpp
  construct DeviceAvailabilityEventsDB
  call DeviceAvailabilityEventsDB->Create()
  include availability retention cleanup if retention should match board timepoint cleanup
```

Add a DB upgrade/migration path for the new table. Existing deployments will start with no historical availability events; the API should return `offline_count: 0` for successful empty queries, not infer old events from `lastDisconnection`.

Add the files to `CMakeLists.txt`.

### Ingestion Hook

Persist availability events from connection handling:

```text
src/APStats.cpp
  AP::UpdateConnection(...)
```

Rules:

```text
On disconnection:
  store one offline event when the device transitions from connected to disconnected.

On ping/capabilities:
  store one online event only when the device transitions from disconnected to connected.

Do not store online events for every ping.
```

The event writer must include `resolvedBoardId`/`board_id`, `serialNumber` set from request `routerId`, `event_type`, and the event timestamp. If the incoming connection message has a stable event id or session id, persist it and use `event_id` for idempotency.

### Store Only State Transitions

Correct:

```text
offline → online: store online event
online → offline: store offline event
```

Incorrect:

```text
every ping → store online event
```

Every ping should not be treated as a new online transition.

### Calculation

```text
offline_count =
    COUNT(event_type = 'offline')
```

### Query

```sql
SELECT COUNT(*) AS offline_count
FROM device_availability_events
WHERE board_id = :board_id
  AND serialNumber = :router_id
  AND event_type = 'offline'
  AND event_time >= :start_time
  AND event_time <= :end_time;
```

### Success Response with No Offline Events

```json
{
  "gw_uuid": "60cf84f22290",
  "fetch_status": "success",
  "offline_count": 0
}
```

### Internal Error

Use an HTTP error:

```http
500 Internal Server Error
```

```json
{
  "error": "availability_query_failed",
  "message": "Unable to retrieve gateway availability history"
}
```

Do not return `fetch_status: failed` with HTTP 200 for internal failures.

---

# Repository Implementation Structure

Repository:

```text
routerarchitects/ra-wlan-cloud-analytics
```

## OpenAPI

Update:

```text
openapi/owanalytics.yaml
```

Add:

```yaml
/devices/{routerId}/memory-summary:
/devices/{routerId}/radio-temperature-summary:
/devices/{routerId}/availability-summary:
/devices/{routerId}/wifi-clients/usage-summary:
/devices/{routerId}/wifi-clients/rssi-summary:
```

---

## REST Objects

Update:

```text
src/RESTObjects/RESTAPI_AnalyticsObjects.h
src/RESTObjects/RESTAPI_AnalyticsObjects.cpp
```

Add objects:

```cpp
struct GatewayMemorySummary;
struct GatewayWifiTemperatureSummary;
struct ClientBandwidthConsumption;
struct ClientRssiQuality;
struct GatewayOfflineSummary;
```

JSON field names should match the MCP CSV exactly.

---

## REST Handlers

Recommended files:

```text
src/RESTAPI/RESTAPI_device_metrics_summary_handler.h
src/RESTAPI/RESTAPI_device_metrics_summary_handler.cpp

src/RESTAPI/RESTAPI_wifi_client_metrics_handler.h
src/RESTAPI/RESTAPI_wifi_client_metrics_handler.cpp
```

Device metrics handler:

```text
memory-summary
radio-temperature-summary
availability-summary
```

Wi-Fi client metrics handler:

```text
usage-summary
rssi-summary
```

All handlers must call a shared serial-resolution helper before storage access:

```cpp
RouterIdResolutionResult ResolveRouterIdContext(
    RESTAPIHandler* client,
    const std::string& routerId);
```

The helper must use status-aware OWPROV inventory lookup, read `InventoryTag.venue`, and resolve board ownership with `monitorSubVenues` support. It can either scan candidate board device lists through `SDK::Prov::Venue::GetDevices(..., VenueInfo.monitorSubVenues, ...)` or use a current `routerId -> boardId` map maintained by `VenueCoordinator`.

---

## Router Registration

Update:

```text
src/RESTAPI/RESTAPI_routers.cpp
```

Register the new handlers in both:

```text
RESTAPI_Router
RESTAPI_Router_I
```

---

## Build Configuration

Update:

```text
CMakeLists.txt
```

Add all new handler and storage `.cpp` and `.h` files.

---

## Storage Files

Update:

```text
src/storage/storage_timepoints.h
src/storage/storage_timepoints.cpp
src/storage/storage_wificlients.h
src/storage/storage_wificlients.cpp
src/storage/storage_device_availability_events.h
src/storage/storage_device_availability_events.cpp
src/StorageService.h
src/StorageService.cpp
```

`storage_wificlients.*` only needs changes if the implementation chooses to add gateway `serialNumber` to `wificlienthistory`. The default implementation for gateway-scoped client summaries should aggregate `timepoints.ssid_data` to avoid that migration.

Suggested functions:

```cpp
bool GetMemorySummary(
    const std::string& resolvedBoardId,
    const std::string& routerId,
    uint64_t startTime,
    uint64_t endTime,
    AnalyticsObjects::GatewayMemorySummary& result);

bool GetRadioTemperatureSummary(...);

bool GetWifiClientUsageSummary(...);

bool GetWifiClientRssiSummary(...);

bool GetGatewayAvailabilitySummary(...);
```

---

# Final Mapping

| MCP Tool | Analytics API | Data Source | Implementation Status |
|---|---|---|---|
| `get_gateway_free_memory` | `GET /devices/{routerId}/memory-summary` | `timepoints.resource_data.memory_free` | Resolve routerId to venueId and boardId, then aggregate `timepoints` |
| `get_gateway_wifi_temp` | `GET /devices/{routerId}/radio-temperature-summary` | `timepoints.radio_data[].temperature` | Resolve routerId to venueId and boardId, then aggregate `timepoints` |
| `get_device_bandwidth_consumption` | `GET /devices/{routerId}/wifi-clients/usage-summary` | `timepoints.ssid_data[].associations[]` | Resolve routerId to venueId and boardId, then reset-safe delta aggregation with pre-window baseline |
| `get_device_rssi_quality` | `GET /devices/{routerId}/wifi-clients/rssi-summary` | `timepoints.ssid_data[].associations[].rssi` | Resolve routerId to venueId and boardId, then classify RSSI samples |
| `get_gateway_offline_count` | `GET /devices/{routerId}/availability-summary` | Existing gateway `connection` topic | Resolve routerId to venueId and boardId, then aggregate persisted offline state transitions |

---

# Recommended Implementation Order

1. `get_gateway_wifi_temp`
2. `get_device_rssi_quality`
3. `get_device_bandwidth_consumption`
4. `get_gateway_free_memory`
5. `get_gateway_offline_count`
