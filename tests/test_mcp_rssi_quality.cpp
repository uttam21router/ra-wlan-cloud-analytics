#include "RESTAPI/RESTAPI_mcp_helpers.h"

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace OpenWifi {
	const std::string &MicroServiceDataDirectory() {
		static const std::string DataDirectory = "/tmp";
		return DataDirectory;
	}

	std::string MicroServiceCreateUUID() {
		return "test-uuid";
	}
} // namespace OpenWifi

using namespace OpenWifi;

namespace {

	MCP::Window TestWindow() {
		MCP::Window W;
		W.startTime = 1000;
		W.endTime = 2000;
		W.lookbackHours = 1;
		return W;
	}

	AnalyticsObjects::UETimePoint Assoc(const std::string &Station, int64_t Rssi) {
		AnalyticsObjects::UETimePoint UE;
		UE.station = Station;
		UE.rssi = Rssi;
		return UE;
	}

	AnalyticsObjects::DeviceTimePoint Point(
		uint64_t Timestamp, std::initializer_list<AnalyticsObjects::UETimePoint> Assocs,
		const std::string &BSSID = "aa:bb:cc:dd:ee:ff", const std::string &SSID = "main",
		uint64_t Band = 5, const std::string &Id = "") {
		AnalyticsObjects::DeviceTimePoint P;
		P.id = Id;
		P.timestamp = Timestamp;
		AnalyticsObjects::SSIDTimePoint S;
		S.bssid = BSSID;
		S.ssid = SSID;
		S.band = Band;
		S.associations = Assocs;
		P.ssid_data.push_back(std::move(S));
		return P;
	}

	const AnalyticsObjects::MCPClientRssiItem *FindItem(
		const AnalyticsObjects::MCPClientRssiQualitySummary &Summary,
		const std::string &Mac) {
		for (const auto &Item : Summary.items) {
			if (Item.mac == Mac)
				return &Item;
		}
		return nullptr;
	}

	void TestRssiThresholdsAndBoundaries() {
		// TC-RSSI-001 (-55 -> excellent)
		// TC-RSSI-002 (-40 -> excellent)
		// TC-RSSI-003 (-56 -> good)
		// TC-RSSI-004 (-67 -> good)
		// TC-RSSI-005 (-68 -> fair)
		// TC-RSSI-006 (-75 -> fair)
		// TC-RSSI-007 (-76 -> poor)
		// TC-RSSI-008 (-127 -> poor)
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -55), Assoc("e2:51:95:ed:0f:28", -40),
						  Assoc("e2:51:95:ed:0f:28", -56), Assoc("e2:51:95:ed:0f:28", -67),
						  Assoc("e2:51:95:ed:0f:28", -68), Assoc("e2:51:95:ed:0f:28", -75),
						  Assoc("e2:51:95:ed:0f:28", -76), Assoc("e2:51:95:ed:0f:28", -127)})},
			TestWindow());

		assert(Summary.totalClients == 1);
		assert(Summary.items.size() == 1);
		const auto &Item = Summary.items[0];
		assert(Item.mac == "e2:51:95:ed:0f:28");
		assert(Item.rssi_total_samples == 8);
		assert(Item.rssi_excellent_pct == 25.0); // 2 / 8
		assert(Item.rssi_good_pct == 25.0);      // 2 / 8
		assert(Item.rssi_fair_pct == 25.0);      // 2 / 8
		assert(Item.rssi_poor_pct == 25.0);      // 2 / 8
	}

	void TestInvalidSamplesIgnored() {
		// TC-RSSI-009 (-128 -> ignore)
		// TC-RSSI-010 (0 -> ignore)
		// TC-RSSI-011 (20 -> ignore)
		// TC-RSSI-018 (mixed valid and invalid)
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("aa:bb:cc:dd:ee:01", -50), Assoc("aa:bb:cc:dd:ee:01", -60),
						  Assoc("aa:bb:cc:dd:ee:01", -70), Assoc("aa:bb:cc:dd:ee:01", -80),
						  Assoc("aa:bb:cc:dd:ee:01", 0), Assoc("aa:bb:cc:dd:ee:01", 20),
						  Assoc("aa:bb:cc:dd:ee:01", -128)})},
			TestWindow());

		assert(Summary.totalClients == 1);
		const auto *Item = FindItem(Summary, "aa:bb:cc:dd:ee:01");
		assert(Item != nullptr);
		assert(Item->rssi_total_samples == 4);
		assert(Item->rssi_excellent_pct == 25.0);
		assert(Item->rssi_good_pct == 25.0);
		assert(Item->rssi_fair_pct == 25.0);
		assert(Item->rssi_poor_pct == 25.0);
	}

	void TestPercentageDecimalRounding() {
		// TC-RSSI-014 (1 excellent, 1 good, 1 fair out of 3 -> 33.33%, 33.33%, 33.33%, 0.00%)
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("28:39:26:a1:7c:a5", -50), Assoc("28:39:26:a1:7c:a5", -60),
						  Assoc("28:39:26:a1:7c:a5", -70)})},
			TestWindow());

		const auto *Item = FindItem(Summary, "28:39:26:a1:7c:a5");
		assert(Item != nullptr);
		assert(Item->rssi_total_samples == 3);
		assert(Item->rssi_excellent_pct == 33.33);
		assert(Item->rssi_good_pct == 33.33);
		assert(Item->rssi_fair_pct == 33.33);
		assert(Item->rssi_poor_pct == 0.0);
	}

	void TestMacNormalizationAndBssidMovement() {
		// TC-RSSI-015, TC-RSSI-020, TC-RSSI-021
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1100, {Assoc("E25195ED0F28", -50)}, "bssid-1"),
			 Point(1300, {Assoc("e2:51:95:ed:0f:28", -60)}, "bssid-2"),
			 Point(1500, {Assoc("e2-51-95-ed-0f-28", -70)}, "bssid-2")},
			TestWindow());

		assert(Summary.totalClients == 1);
		assert(Summary.items.size() == 1);
		assert(Summary.items[0].mac == "e2:51:95:ed:0f:28");
		assert(Summary.items[0].rssi_total_samples == 3);
		assert(Summary.meta.observedWindow.startTime == std::nullopt); // RSSI has no meta
		assert(Summary.observedWindow.startTime == "1970-01-01T00:18:20Z");
		assert(Summary.observedWindow.endTime == "1970-01-01T00:25:00Z");
	}

	void TestNoClientsAndInvalidSamplesOnly() {
		// TC-RSSI-022, TC-RSSI-023
		auto SummaryEmpty = MCP::CalculateDeviceRssiQualitySummary({}, TestWindow());
		assert(SummaryEmpty.totalClients == 0);
		assert(SummaryEmpty.items.empty());
		assert(!SummaryEmpty.truncated);
		assert(!SummaryEmpty.observedWindow.startTime);
		assert(!SummaryEmpty.observedWindow.endTime);

		auto SummaryInvalidOnly = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("aa:bb:cc:dd:ee:02", 0), Assoc("aa:bb:cc:dd:ee:02", -200)})},
			TestWindow());
		assert(SummaryInvalidOnly.totalClients == 0);
		assert(SummaryInvalidOnly.items.empty());
	}

	void TestOutsideWindowSamplesExcluded() {
		// TC-RSSI-024
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(900, {Assoc("aa:bb:cc:dd:ee:03", -50)}),
			 Point(1500, {Assoc("aa:bb:cc:dd:ee:03", -60)}),
			 Point(2100, {Assoc("aa:bb:cc:dd:ee:03", -70)})},
			TestWindow());

		assert(Summary.totalClients == 1);
		assert(Summary.items.size() == 1);
		assert(Summary.items[0].rssi_total_samples == 1);
		assert(Summary.items[0].rssi_good_pct == 100.0);
		assert(Summary.observedWindow.startTime == "1970-01-01T00:25:00Z");
		assert(Summary.observedWindow.endTime == "1970-01-01T00:25:00Z");
	}

	void TestTruncationSortingAndObservedWindowIsolation() {
		// TC-RSSI-027
		std::vector<AnalyticsObjects::DeviceTimePoint> Records;
		for (int i = 0; i < 501; ++i) {
			char macBuf[18];
			std::snprintf(macBuf, sizeof(macBuf), "00:00:00:00:%02x:%02x", i / 256, i % 256);
			uint64_t ts = (i == 500) ? 1900 : 1500; // 501st client has unique 1900 timestamp
			Records.push_back(Point(ts, {Assoc(macBuf, -50)}));
		}

		auto Summary = MCP::CalculateDeviceRssiQualitySummary(Records, TestWindow());
		assert(Summary.totalClients == 501);
		assert(Summary.truncated);
		assert(Summary.items.size() == 500);

		// Verify MAC ASC sorting
		for (size_t i = 1; i < Summary.items.size(); ++i) {
			assert(Summary.items[i - 1].mac < Summary.items[i].mac);
		}

		// The 501st client (00:00:00:00:01:f4) is excluded from items[].
		// Its timestamp at 1900 must NOT extend observedWindow.endTime!
		assert(Summary.observedWindow.startTime == "1970-01-01T00:25:00Z");
		assert(Summary.observedWindow.endTime == "1970-01-01T00:25:00Z");
	}

	void TestSerializationShape() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("aa:bb:cc:dd:ee:06", -50)})}, TestWindow());

		Poco::JSON::Object Obj;
		Summary.to_json(Obj);

		std::stringstream ss;
		Obj.stringify(ss);
		Poco::JSON::Parser parser;
		auto ParsedObj = parser.parse(ss).extract<Poco::JSON::Object::Ptr>();

		assert(ParsedObj->has("requestedWindow"));
		assert(ParsedObj->has("observedWindow"));
		assert(ParsedObj->has("items"));
		assert(ParsedObj->has("totalClients"));
		assert(ParsedObj->has("truncated"));

		auto Items = ParsedObj->getArray("items");
		assert(Items->size() == 1);
		auto Item = Items->getObject(0);
		assert(Item->has("mac"));
		assert(Item->has("rssi_excellent_pct"));
		assert(Item->has("rssi_good_pct"));
		assert(Item->has("rssi_fair_pct"));
		assert(Item->has("rssi_poor_pct"));
		assert(Item->has("rssi_total_samples"));
	}

} // namespace

int main() {
	TestRssiThresholdsAndBoundaries();
	TestInvalidSamplesIgnored();
	TestPercentageDecimalRounding();
	TestMacNormalizationAndBssidMovement();
	TestNoClientsAndInvalidSamplesOnly();
	TestOutsideWindowSamplesExcluded();
	TestTruncationSortingAndObservedWindowIsolation();
	TestSerializationShape();

	std::cout << "test_mcp_rssi_quality passed\n";
	return 0;
}
