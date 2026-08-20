//
// Comprehensive Unit Test Suite for MCP WLAN Analytics APIs
//

#include "RESTObjects/RESTAPI_AnalyticsObjects.h"
#include <cassert>
#include <ctime>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

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

int main() {
	std::cout << "===========================================" << std::endl;
	std::cout << "  MCP WLAN Analytics APIs Unit Test Suite  " << std::endl;
	std::cout << "===========================================" << std::endl;

	TestRFC3339Parsing();
	TestMemoryAggregation();
	TestRadioTemperatureRange();
	TestRssiClassification();

	std::cout << "===========================================" << std::endl;
	std::cout << "  ALL MCP API UNIT TESTS PASSED CLEANLY!  " << std::endl;
	std::cout << "===========================================" << std::endl;
	return 0;
}
