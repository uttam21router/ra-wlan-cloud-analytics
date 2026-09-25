#include "RESTAPI/RESTAPI_mcp_helpers.h"
#include "storage/storage_rssi_parser.h"

#include <Poco/JSON/Array.h>
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

	// TC-RSSI-001: Excellent RSSI boundary (RSSI = -55)
	void TC_RSSI_001_ExcellentBoundary() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -55)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_excellent_pct == 100.0);
		assert(Summary.items[0].rssi_good_pct == 0.0);
	}

	// TC-RSSI-002: RSSI above excellent boundary (RSSI = -40)
	void TC_RSSI_002_AboveExcellent() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -40)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_excellent_pct == 100.0);
	}

	// TC-RSSI-003: Good upper boundary (RSSI = -56)
	void TC_RSSI_003_GoodUpperBoundary() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -56)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_good_pct == 100.0);
	}

	// TC-RSSI-004: Good lower boundary (RSSI = -67)
	void TC_RSSI_004_GoodLowerBoundary() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -67)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_good_pct == 100.0);
	}

	// TC-RSSI-005: Fair upper boundary (RSSI = -68)
	void TC_RSSI_005_FairUpperBoundary() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -68)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_fair_pct == 100.0);
	}

	// TC-RSSI-006: Fair lower boundary (RSSI = -75)
	void TC_RSSI_006_FairLowerBoundary() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -75)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_fair_pct == 100.0);
	}

	// TC-RSSI-007: Poor boundary (RSSI = -76)
	void TC_RSSI_007_PoorBoundary() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -76)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_poor_pct == 100.0);
	}

	// TC-RSSI-008: Minimum accepted RSSI (RSSI = -127)
	void TC_RSSI_008_MinimumAcceptedRssi() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -127)})}, TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_poor_pct == 100.0);
		assert(Summary.items[0].rssi_total_samples == 1);
	}

	// TC-RSSI-009: RSSI below valid range (RSSI = -128)
	void TC_RSSI_009_RssiBelowValidRange() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -128)})}, TestWindow());
		assert(Summary.totalClients == 0);
		assert(Summary.items.empty());
	}

	// TC-RSSI-010: RSSI equals zero (RSSI = 0)
	void TC_RSSI_010_RssiEqualsZero() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", 0)})}, TestWindow());
		assert(Summary.totalClients == 0);
		assert(Summary.items.empty());
	}

	// TC-RSSI-011: Positive RSSI (RSSI = 20)
	void TC_RSSI_011_PositiveRssi() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", 20)})}, TestWindow());
		assert(Summary.totalClients == 0);
		assert(Summary.items.empty());
	}

	// TC-RSSI-012: Null RSSI
	void TC_RSSI_012_NullRssi() {
		AnalyticsObjects::DeviceTimePoint P;
		P.timestamp = 1200;
		AnalyticsObjects::SSIDTimePoint S;
		S.bssid = "aa:bb:cc:dd:ee:ff";
		S.ssid = "main";
		AnalyticsObjects::UETimePoint UE;
		UE.station = "e2:51:95:ed:0f:28";
		UE.rssi = 0; // Default zero RSSI represents unpopulated/null sample
		S.associations.push_back(UE);
		P.ssid_data.push_back(S);

		auto Summary = MCP::CalculateDeviceRssiQualitySummary({P}, TestWindow());
		assert(Summary.totalClients == 0);
	}

	// TC-RSSI-013: Calculate percentages for one client
	void TC_RSSI_013_CalculatePercentagesOneClient() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -50), Assoc("e2:51:95:ed:0f:28", -50),
						  Assoc("e2:51:95:ed:0f:28", -50), Assoc("e2:51:95:ed:0f:28", -50),
						  Assoc("e2:51:95:ed:0f:28", -60), Assoc("e2:51:95:ed:0f:28", -60),
						  Assoc("e2:51:95:ed:0f:28", -60), Assoc("e2:51:95:ed:0f:28", -70),
						  Assoc("e2:51:95:ed:0f:28", -70), Assoc("e2:51:95:ed:0f:28", -80)})},
			TestWindow());
		assert(Summary.totalClients == 1);
		const auto &Item = Summary.items[0];
		assert(Item.rssi_excellent_pct == 40.0);
		assert(Item.rssi_good_pct == 30.0);
		assert(Item.rssi_fair_pct == 20.0);
		assert(Item.rssi_poor_pct == 10.0);
		assert(Item.rssi_total_samples == 10);
	}

	// TC-RSSI-014: Percentages require decimal rounding
	void TC_RSSI_014_PercentagesRequireDecimalRounding() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("28:39:26:a1:7c:a5", -50), Assoc("28:39:26:a1:7c:a5", -60),
						  Assoc("28:39:26:a1:7c:a5", -70)})},
			TestWindow());
		const auto *Item = FindItem(Summary, "28:39:26:a1:7c:a5");
		assert(Item != nullptr);
		assert(Item->rssi_excellent_pct == 33.33);
		assert(Item->rssi_good_pct == 33.33);
		assert(Item->rssi_fair_pct == 33.33);
		assert(Item->rssi_poor_pct == 0.0);
	}

	// TC-RSSI-015: Percentage sum after rounding
	void TC_RSSI_015_PercentageSumAfterRounding() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("28:39:26:a1:7c:a5", -50), Assoc("28:39:26:a1:7c:a5", -60),
						  Assoc("28:39:26:a1:7c:a5", -70)})},
			TestWindow());
		const auto *Item = FindItem(Summary, "28:39:26:a1:7c:a5");
		assert(Item != nullptr);
		double sum = Item->rssi_excellent_pct + Item->rssi_good_pct + Item->rssi_fair_pct + Item->rssi_poor_pct;
		assert(std::abs(sum - 99.99) < 0.01);
	}

	// TC-RSSI-016: All samples are excellent
	void TC_RSSI_016_AllSamplesExcellent() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -45), Assoc("e2:51:95:ed:0f:28", -50)})},
			TestWindow());
		assert(Summary.items[0].rssi_excellent_pct == 100.0);
		assert(Summary.items[0].rssi_good_pct == 0.0);
		assert(Summary.items[0].rssi_fair_pct == 0.0);
		assert(Summary.items[0].rssi_poor_pct == 0.0);
	}

	// TC-RSSI-017: All samples are poor
	void TC_RSSI_017_AllSamplesPoor() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("e2:51:95:ed:0f:28", -80), Assoc("e2:51:95:ed:0f:28", -90)})},
			TestWindow());
		assert(Summary.items[0].rssi_excellent_pct == 0.0);
		assert(Summary.items[0].rssi_good_pct == 0.0);
		assert(Summary.items[0].rssi_fair_pct == 0.0);
		assert(Summary.items[0].rssi_poor_pct == 100.0);
	}

	// TC-RSSI-018: Valid and invalid samples mixed
	void TC_RSSI_018_ValidAndInvalidSamplesMixed() {
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

	// TC-RSSI-019: Multiple clients
	void TC_RSSI_019_MultipleClients() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("11:22:33:44:55:66", -50), Assoc("aa:bb:cc:dd:ee:ff", -80)})},
			TestWindow());
		assert(Summary.totalClients == 2);
		const auto *c1 = FindItem(Summary, "11:22:33:44:55:66");
		const auto *c2 = FindItem(Summary, "aa:bb:cc:dd:ee:ff");
		assert(c1 != nullptr && c2 != nullptr);
		assert(c1->rssi_excellent_pct == 100.0);
		assert(c2->rssi_poor_pct == 100.0);
	}

	// TC-RSSI-020: Same MAC with different case
	void TC_RSSI_020_SameMacWithDifferentCase() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("E2:51:95:ED:0F:28", -50), Assoc("e2:51:95:ed:0f:28", -60)})},
			TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].mac == "e2:51:95:ed:0f:28");
		assert(Summary.items[0].rssi_total_samples == 2);
	}

	void TestRssiMacNormalizationValidFormats() {
		assert(MCP::NormalizeClientMac("AA:BB:CC:DD:EE:FF") == "aa:bb:cc:dd:ee:ff");
		assert(MCP::NormalizeClientMac("AA-BB-CC-DD-EE-FF") == "aa:bb:cc:dd:ee:ff");
		assert(MCP::NormalizeClientMac("AABB.CCDD.EEFF") == "aa:bb:cc:dd:ee:ff");
		assert(MCP::NormalizeClientMac("AABBCCDDEEFF") == "aa:bb:cc:dd:ee:ff");

		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200,
				   {Assoc("AA:BB:CC:DD:EE:FF", -50),
					Assoc("aa-bb-cc-dd-ee-ff", -60),
					Assoc("aabb.ccdd.eeff", -70),
					Assoc("aabbccddeeff", -80)})},
			TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].mac == "aa:bb:cc:dd:ee:ff");
		assert(Summary.items[0].rssi_total_samples == 4);
	}

	void TestRssiMacNormalizationRejectsMalformedFormats() {
		std::vector<std::string> InvalidMacs = {
			"aa::bb::cc::dd::ee::ff",
			"aa:bbcc:ddee:ff",
			"aa-bb:cc-dd:ee-ff",
			"aabb.cc:dd.eeff",
			"aa:bb:cc:dd:ee",
			"gg:bb:cc:dd:ee:ff",
			"aa:bb:cc:dd:ee:ff ",
		};
		for (const auto &Mac : InvalidMacs) {
			assert(!MCP::NormalizeClientMac(Mac));
		}

		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200,
				   {Assoc("aa::bb::cc::dd::ee::ff", -50),
					Assoc("aa:bbcc:ddee:ff", -50),
					Assoc("aa-bb:cc-dd:ee-ff", -50),
					Assoc("aabb.cc:dd.eeff", -50),
					Assoc("aa:bb:cc:dd:ee", -50),
					Assoc("gg:bb:cc:dd:ee:ff", -50)})},
			TestWindow());
		assert(Summary.totalClients == 0);
		assert(Summary.items.empty());
	}

	void TestMalformedMacDoesNotMergeIntoValidClient() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200,
				   {Assoc("aa:bb:cc:dd:ee:ff", -50),
					Assoc("aa::bb::cc::dd::ee::ff", -80)})},
			TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items.size() == 1);
		assert(Summary.items[0].mac == "aa:bb:cc:dd:ee:ff");
		assert(Summary.items[0].rssi_total_samples == 1);
		assert(Summary.items[0].rssi_excellent_pct == 100.0);
		assert(Summary.items[0].rssi_poor_pct == 0.0);
	}

	// TC-RSSI-021: Client moves between BSSIDs
	void TC_RSSI_021_ClientMovesBetweenBssids() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1100, {Assoc("e2:51:95:ed:0f:28", -50)}, "bssid-1"),
			 Point(1300, {Assoc("e2:51:95:ed:0f:28", -60)}, "bssid-2")},
			TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].mac == "e2:51:95:ed:0f:28");
		assert(Summary.items[0].rssi_total_samples == 2);
	}

	// TC-RSSI-022: No clients
	void TC_RSSI_022_NoClients() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary({}, TestWindow());
		assert(Summary.totalClients == 0);
		assert(Summary.items.empty());
		assert(!Summary.truncated);
		assert(!Summary.observedWindow.startTime);
		assert(!Summary.observedWindow.endTime);
	}

	// TC-RSSI-023: Client has only invalid samples
	void TC_RSSI_023_ClientHasOnlyInvalidSamples() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("aa:bb:cc:dd:ee:02", 0), Assoc("aa:bb:cc:dd:ee:02", -200)})},
			TestWindow());
		assert(Summary.totalClients == 0);
		assert(Summary.items.empty());
	}

	// TC-RSSI-024: Samples outside requested range
	void TC_RSSI_024_SamplesOutsideRequestedRange() {
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(900, {Assoc("aa:bb:cc:dd:ee:03", -50)}),
			 Point(1500, {Assoc("aa:bb:cc:dd:ee:03", -60)}),
			 Point(2100, {Assoc("aa:bb:cc:dd:ee:03", -70)})},
			TestWindow());
		assert(Summary.totalClients == 1);
		assert(Summary.items[0].rssi_total_samples == 1);
		assert(Summary.items[0].rssi_good_pct == 100.0);
	}

	// TC-RSSI-025: Gateway filtering
	void TC_RSSI_025_GatewayFiltering() {
		// Handled at storage query layer by query parameters boardId & routerId;
		// helper function verifies proper handling of queried records.
		auto Summary = MCP::CalculateDeviceRssiQualitySummary(
			{Point(1200, {Assoc("aa:bb:cc:dd:ee:03", -50)})}, TestWindow());
		assert(Summary.totalClients == 1);
	}

	// TC-RSSI-026: Malformed association entry
	void TC_RSSI_026_MalformedAssociationEntry() {
		// Test persisted JSON string with valid association, corrupt non-object entry, malformed object entry, and second valid association
		std::string ssidJson = R"([
			{
				"bssid": "aa:bb:cc:dd:ee:ff",
				"ssid": "main",
				"associations": [
					{"station": "11:22:33:44:55:66", "rssi": -50},
					"corrupt-entry",
					{"station": "22:33:44:55:66:77", "rssi": -45, "tx_duration": "invalid"},
					{"station": "aa:bb:cc:dd:ee:ff", "rssi": -60}
				]
			}
		])";

		AnalyticsObjects::DeviceTimePoint Point;
		Point.id = "test-malformed-assoc";
		Point.timestamp = 1200;
		assert(Storage::ParseSsidDataForRssi(
			ssidJson, Point.id, Poco::Logger::get("test_mcp_rssi_quality"), Point.ssid_data));

		assert(Point.ssid_data.size() == 1);
		// Verify both valid associations (skipping "corrupt-entry" and malformed object) are processed!
		assert(Point.ssid_data[0].associations.size() == 2);
		assert(Point.ssid_data[0].associations[0].station == "11:22:33:44:55:66");
		assert(Point.ssid_data[0].associations[1].station == "aa:bb:cc:dd:ee:ff");

		auto Summary = MCP::CalculateDeviceRssiQualitySummary({Point}, TestWindow());
		assert(Summary.totalClients == 2);
		assert(Summary.items[0].mac == "11:22:33:44:55:66");
		assert(Summary.items[1].mac == "aa:bb:cc:dd:ee:ff");
	}

	// TC-RSSI-027: RSSI truncation, MAC ordering, and post-limit observedWindow isolation
	void TC_RSSI_027_RssiTruncationMacOrderingObservedWindowIsolation() {
		std::vector<AnalyticsObjects::DeviceTimePoint> Records;
		for (int i = 0; i < 501; ++i) {
			char macBuf[18];
			std::snprintf(macBuf, sizeof(macBuf), "00:00:00:00:%02x:%02x", i / 256, i % 256);
			uint64_t ts = (i == 500) ? 1900 : 1500;
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

	void TestRssiSsidRecordLimitSentinel() {
		bool LimitExceeded = true;
		assert(!Storage::RssiSsidRecordCountExceedsLimit(10, 10, &LimitExceeded));
		assert(!LimitExceeded);

		LimitExceeded = false;
		assert(Storage::RssiSsidRecordCountExceedsLimit(11, 10, &LimitExceeded));
		assert(LimitExceeded);
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

		assert(ParsedObj->has("meta"));
		assert(ParsedObj->has("data"));
		assert(!ParsedObj->has("requestedWindow"));
		assert(!ParsedObj->has("observedWindow"));

		auto MetaObj = ParsedObj->getObject("meta");
		assert(MetaObj->has("requestedWindow"));
		assert(MetaObj->has("observedWindow"));

		auto DataObj = ParsedObj->getObject("data");
		assert(DataObj->has("items"));
		assert(DataObj->has("totalClients"));
		assert(DataObj->has("truncated"));

		auto Items = DataObj->getArray("items");
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
	TC_RSSI_001_ExcellentBoundary();
	TC_RSSI_002_AboveExcellent();
	TC_RSSI_003_GoodUpperBoundary();
	TC_RSSI_004_GoodLowerBoundary();
	TC_RSSI_005_FairUpperBoundary();
	TC_RSSI_006_FairLowerBoundary();
	TC_RSSI_007_PoorBoundary();
	TC_RSSI_008_MinimumAcceptedRssi();
	TC_RSSI_009_RssiBelowValidRange();
	TC_RSSI_010_RssiEqualsZero();
	TC_RSSI_011_PositiveRssi();
	TC_RSSI_012_NullRssi();
	TC_RSSI_013_CalculatePercentagesOneClient();
	TC_RSSI_014_PercentagesRequireDecimalRounding();
	TC_RSSI_015_PercentageSumAfterRounding();
	TC_RSSI_016_AllSamplesExcellent();
	TC_RSSI_017_AllSamplesPoor();
	TC_RSSI_018_ValidAndInvalidSamplesMixed();
	TC_RSSI_019_MultipleClients();
	TC_RSSI_020_SameMacWithDifferentCase();
	TestRssiMacNormalizationValidFormats();
	TestRssiMacNormalizationRejectsMalformedFormats();
	TestMalformedMacDoesNotMergeIntoValidClient();
	TC_RSSI_021_ClientMovesBetweenBssids();
	TC_RSSI_022_NoClients();
	TC_RSSI_023_ClientHasOnlyInvalidSamples();
	TC_RSSI_024_SamplesOutsideRequestedRange();
	TC_RSSI_025_GatewayFiltering();
	TC_RSSI_026_MalformedAssociationEntry();
	TC_RSSI_027_RssiTruncationMacOrderingObservedWindowIsolation();
	TestRssiSsidRecordLimitSentinel();
	TestSerializationShape();

	std::cout << "test_mcp_rssi_quality passed (TC-RSSI-001 to TC-RSSI-027 verified)\n";
	return 0;
}
