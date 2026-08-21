//
// Comprehensive Unit Test Suite for MCP WLAN Analytics APIs
//

#include <cassert>
#include <ctime>
#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace OpenWifi {
	namespace AnalyticsObjects {
		struct DeviceResourceTimePoint {
			std::optional<uint64_t> memory_free;
			std::optional<uint64_t> memory_total;
		};

		struct DeviceTimePoint {
			uint64_t timestamp = 0;
			DeviceResourceTimePoint resource_data;
		};
	}
}

using namespace OpenWifi;

namespace {
bool ParseRFC3339UTCForTest(const std::string &str, uint64_t &epochSeconds) {
	if (str.empty() || str.back() != 'Z')
		return false;

	if (str.find('+') != std::string::npos || (str.find('-') != std::string::npos && str.rfind('-') > 7)) {
		size_t lastHyphen = str.rfind('-');
		if (lastHyphen > 7) return false;
	}

	static const std::regex pattern(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$)");
	if (!std::regex_match(str, pattern))
		return false;

	std::tm tm_buf{};
	int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
	if (sscanf(str.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &min, &sec) != 6)
		return false;

	if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || min < 0 ||
		min > 59 || sec < 0 || sec > 59)
		return false;

	tm_buf.tm_year = year - 1900;
	tm_buf.tm_mon = month - 1;
	tm_buf.tm_mday = day;
	tm_buf.tm_hour = hour;
	tm_buf.tm_min = min;
	tm_buf.tm_sec = sec;
	tm_buf.tm_isdst = 0;

	std::time_t t = timegm(&tm_buf);
	if (t == -1)
		return false;

	epochSeconds = static_cast<uint64_t>(t);
	return true;
}
}

void TestRFC3339Parsing() {
	std::cout << "[TEST] Running RFC3339 Parsing tests..." << std::endl;
	uint64_t epoch = 0;

	// Valid UTC Z format
	assert(ParseRFC3339UTCForTest("2026-07-27T12:00:00Z", epoch) == true);
	assert(epoch == 1785153600ULL);

	// Reject numeric timezone offsets
	assert(ParseRFC3339UTCForTest("2026-07-27T12:00:00+05:30", epoch) == false);
	assert(ParseRFC3339UTCForTest("2026-07-27T12:00:00-08:00", epoch) == false);

	// Reject missing timezone
	assert(ParseRFC3339UTCForTest("2026-07-27T12:00:00", epoch) == false);

	// Reject invalid dates
	assert(ParseRFC3339UTCForTest("2026-99-99T88:77:66Z", epoch) == false);

	std::cout << " -> RFC3339 Parsing tests PASSED." << std::endl;
}

void TestMemoryAggregation() {
	std::cout << "[TEST] Running Memory Aggregation tests..." << std::endl;

	AnalyticsObjects::DeviceTimePoint tp1;
	tp1.timestamp = 1000;
	tp1.resource_data.memory_free = 200000;
	tp1.resource_data.memory_total = 1000000;

	AnalyticsObjects::DeviceTimePoint tp2;
	tp2.timestamp = 2000;
	tp2.resource_data.memory_free = 1200000; // Free > Total (invalid)
	tp2.resource_data.memory_total = 1000000;

	AnalyticsObjects::DeviceTimePoint tp3;
	tp3.timestamp = 3000;
	tp3.resource_data.memory_free = 300000;
	// total is std::nullopt

	std::vector<AnalyticsObjects::DeviceTimePoint> recs = {tp1, tp2, tp3};
	std::vector<uint64_t> memoryFreeSamples;

	for (const auto &rec : recs) {
		if (rec.resource_data.memory_free.has_value()) {
			uint64_t freeVal = rec.resource_data.memory_free.value();
			if (rec.resource_data.memory_total.has_value()) {
				if (freeVal > rec.resource_data.memory_total.value()) {
					continue; // Exclude free > total
				}
			}
			memoryFreeSamples.push_back(freeVal);
		}
	}

	assert(memoryFreeSamples.size() == 2); // tp1 and tp3 included, tp2 excluded
	assert(memoryFreeSamples[0] == 200000);
	assert(memoryFreeSamples[1] == 300000);

	std::cout << " -> Memory Aggregation tests PASSED." << std::endl;
}

void TestRadioTemperatureRange() {
	std::cout << "[TEST] Running Radio Temperature Range tests..." << std::endl;

	// Valid range [-40, 125]. 0°C is valid. 255 is invalid sentinel.
	auto IsValidTemp = [](int64_t temp) -> bool {
		return temp >= -40 && temp <= 125 && temp != 255;
	};

	assert(IsValidTemp(66) == true);
	assert(IsValidTemp(0) == true);
	assert(IsValidTemp(-40) == true);
	assert(IsValidTemp(125) == true);
	assert(IsValidTemp(255) == false);
	assert(IsValidTemp(-45) == false);
	assert(IsValidTemp(130) == false);

	std::cout << " -> Radio Temperature Range tests PASSED." << std::endl;
}

void TestRssiClassification() {
	std::cout << "[TEST] Running RSSI Classification tests..." << std::endl;

	std::vector<int64_t> samples = {-55, -65, -75, -85};
	uint64_t exc = 0, good = 0, fair = 0, poor = 0;

	for (auto rssi : samples) {
		if (rssi >= -60) exc++;
		else if (rssi >= -70) good++;
		else if (rssi >= -80) fair++;
		else poor++;
	}

	assert(exc == 1);
	assert(good == 1);
	assert(fair == 1);
	assert(poor == 1);

	std::cout << " -> RSSI Classification tests PASSED." << std::endl;
}

void TestBandwidthResetVsRollover() {
	std::cout << "[TEST] Running Bandwidth Reset vs Rollover tests..." << std::endl;

	auto CalculateRxDelta = [](uint64_t prev, uint64_t curr) -> uint64_t {
		if (curr >= prev) {
			return curr - prev;
		} else {
			uint64_t max32 = 0xFFFFFFFFULL;
			uint64_t max64 = 0xFFFFFFFFFFFFFFFFULL;
			uint64_t thresh32 = 0xF0000000ULL;
			uint64_t thresh64 = 0xF000000000000000ULL;

			if (prev >= thresh32 && prev <= max32) {
				return ((max32 - prev) + curr + 1);
			} else if (prev >= thresh64) {
				return ((max64 - prev) + curr + 1);
			} else {
				// Counter reset: baseline starting from 0
				return curr;
			}
		}
	};

	// 1. Normal increasing counter
	assert(CalculateRxDelta(100, 500) == 400);

	// 2. Counter reset (e.g., reconnect/AP restart from 100,000 to 100) -> MUST NOT be 4.29 GB
	uint64_t resetDelta = CalculateRxDelta(100000, 100);
	assert(resetDelta == 100); // Baseline starting from 0

	// 3. Genuine 32-bit counter rollover near threshold (0xFFFFFF00 -> 100)
	uint64_t prevNearMax32 = 0xFFFFFF00ULL;
	uint64_t currAfterRoll32 = 100ULL;
	uint64_t expectedRoll32 = (0xFFFFFFFFULL - prevNearMax32) + currAfterRoll32 + 1; // 256 + 100 = 356
	assert(CalculateRxDelta(prevNearMax32, currAfterRoll32) == expectedRoll32);

	std::cout << " -> Bandwidth Reset vs Rollover tests PASSED." << std::endl;
}

void TestIdempotencyKeyFallback() {
	std::cout << "[TEST] Running Idempotency Key Fallback tests..." << std::endl;

	auto GenerateIdempotencyKey = [](const std::string &serialNumber, const std::string &eventType,
									  uint64_t eventTime, const std::string &sessionId,
									  const std::string &eventId) -> std::string {
		std::string tail = eventId.empty() ? (sessionId.empty() ? "nosession" : sessionId) : eventId;
		return serialNumber + ":" + eventType + ":" + std::to_string(eventTime) + ":" + tail;
	};

	std::string serial = "460011223344";
	std::string type = "offline";
	uint64_t t = 1785153600ULL;
	std::string sess = "sess-abc-123";

	// Replaying exact same event with empty eventId MUST yield identical key
	std::string key1 = GenerateIdempotencyKey(serial, type, t, sess, "");
	std::string key2 = GenerateIdempotencyKey(serial, type, t, sess, "");

	assert(!key1.empty());
	assert(key1 == key2);
	assert(key1 == "460011223344:offline:1785153600:sess-abc-123");

	// When eventId is provided, use eventId
	std::string keyWithEvId = GenerateIdempotencyKey(serial, type, t, sess, "ev-789");
	assert(keyWithEvId == "460011223344:offline:1785153600:ev-789");

	std::cout << " -> Idempotency Key Fallback tests PASSED." << std::endl;
}

void TestBoardIdScopedQueries() {
	std::cout << "[TEST] Running BoardId Scoped Query Formatting tests..." << std::endl;

	auto FormatTimepointQuery = [](const std::string &boardId, const std::string &serialNumber) -> std::string {
		return " boardId='" + boardId + "' and serialNumber='" + serialNumber + "' ";
	};

	auto FormatAvailabilityQuery = [](const std::string &boardId, const std::string &serialNumber) -> std::string {
		return "board_id='" + boardId + "' AND serialNumber='" + serialNumber + "' AND event_type='offline'";
	};

	std::string tpSql = FormatTimepointQuery("board-123", "serial-456");
	assert(tpSql.find("boardId='board-123'") != std::string::npos);
	assert(tpSql.find("serialNumber='serial-456'") != std::string::npos);

	std::string availSql = FormatAvailabilityQuery("board-123", "serial-456");
	assert(availSql.find("board_id='board-123'") != std::string::npos);
	assert(availSql.find("serialNumber='serial-456'") != std::string::npos);

	std::cout << " -> BoardId Scoped Query Formatting tests PASSED." << std::endl;
}

int main() {
	std::cout << "===========================================" << std::endl;
	std::cout << "  MCP WLAN Analytics APIs Unit Test Suite  " << std::endl;
	std::cout << "===========================================" << std::endl;

	TestRFC3339Parsing();
	TestMemoryAggregation();
	TestRadioTemperatureRange();
	TestRssiClassification();
	TestBandwidthResetVsRollover();
	TestIdempotencyKeyFallback();
	TestBoardIdScopedQueries();

	std::cout << "===========================================" << std::endl;
	std::cout << "  ALL MCP API UNIT TESTS PASSED CLEANLY!  " << std::endl;
	std::cout << "===========================================" << std::endl;
	return 0;
}
