#include "RESTAPI/RESTAPI_mcp_helpers.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OpenWifi;

namespace {

	AnalyticsObjects::RadioTimePoint Radio(uint64_t Band, std::optional<int64_t> WifiTemp,
										   std::optional<int64_t> Temperature = std::nullopt) {
		AnalyticsObjects::RadioTimePoint R;
		R.band = Band;
		R.wifi_temp = WifiTemp;
		if (Temperature) {
			R.temperature = *Temperature;
			R.temperature_present = true;
		}
		return R;
	}

	AnalyticsObjects::DeviceTimePoint Point(
		uint64_t Timestamp, std::vector<AnalyticsObjects::RadioTimePoint> Radios,
		std::string Id = "") {
		AnalyticsObjects::DeviceTimePoint P;
		P.id = std::move(Id);
		P.timestamp = Timestamp;
		P.radio_data = std::move(Radios);
		return P;
	}

	MCP::Window TestWindow() {
		MCP::Window W;
		W.startTime = 1000;
		W.endTime = 2000;
		W.lookbackHours = 1;
		return W;
	}

	void AssertNear(double Actual, double Expected) {
		assert(std::fabs(Actual - Expected) < 0.001);
	}

	void TestTwoBandAggregation() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(2, 20), Radio(5, 40)}, "a"),
			 Point(300, {Radio(2, 21), Radio(5, 38)}, "c"),
			 Point(200, {Radio(2, 21), Radio(5, 42)}, "b")},
			TestWindow());

		assert(Summary.data.min_wifi_temp_2_4G == 20);
		assert(Summary.data.max_wifi_temp_2_4G == 21);
		AssertNear(*Summary.data.avg_wifi_temp_2_4G, 20.67);
		assert(Summary.data.latest_wifi_temp_2_4G == 21);

		assert(Summary.data.min_wifi_temp_5G == 38);
		assert(Summary.data.max_wifi_temp_5G == 42);
		AssertNear(*Summary.data.avg_wifi_temp_5G, 40.0);
		assert(Summary.data.latest_wifi_temp_5G == 38);
		assert(Summary.meta.observedWindow.startTime == "1970-01-01T00:01:40Z");
		assert(Summary.meta.observedWindow.endTime == "1970-01-01T00:05:00Z");
	}

	void TestResponseEnvelopeSerialization() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(2, 20), Radio(5, 40)})}, TestWindow());
		Poco::JSON::Object Obj;
		Summary.to_json(Obj);
		assert(Obj.has("data"));
		assert(Obj.has("meta"));
		assert(!Obj.has("min_wifi_temp_2.4G"));

		auto Data = Obj.getObject("data");
		auto Meta = Obj.getObject("meta");
		assert(Data->has("min_wifi_temp_2.4G"));
		assert(Data->has("max_wifi_temp_2.4G"));
		assert(Data->has("avg_wifi_temp_2.4G"));
		assert(Data->has("latest_wifi_temp_2.4G"));
		assert(Data->has("min_wifi_temp_5G"));
		assert(Data->has("max_wifi_temp_5G"));
		assert(Data->has("avg_wifi_temp_5G"));
		assert(Data->has("latest_wifi_temp_5G"));
		assert(Meta->has("requestedWindow"));
		assert(Meta->has("observedWindow"));
	}

	void TestNoSamplesAndUnsupportedBands() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(6, 50), Radio(2, std::nullopt), Radio(5, 255)})},
			TestWindow());
		assert(!Summary.data.min_wifi_temp_2_4G);
		assert(!Summary.data.max_wifi_temp_2_4G);
		assert(!Summary.data.avg_wifi_temp_2_4G);
		assert(!Summary.data.latest_wifi_temp_2_4G);
		assert(!Summary.data.min_wifi_temp_5G);
		assert(!Summary.meta.observedWindow.startTime);
		assert(!Summary.meta.observedWindow.endTime);
	}

	void TestTemperatureValidityBounds() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(2, -40), Radio(2, 125), Radio(2, 0), Radio(2, -41),
						 Radio(2, 126), Radio(2, 255)})},
			TestWindow());
		assert(Summary.data.min_wifi_temp_2_4G == -40);
		assert(Summary.data.max_wifi_temp_2_4G == 125);
		AssertNear(*Summary.data.avg_wifi_temp_2_4G, 28.33);
		assert(Summary.data.latest_wifi_temp_2_4G == 0);
	}

	void TestWifiTempPrecedenceAndFallback() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(2, 20, 80)}), Point(200, {Radio(2, std::nullopt, 30)}),
			 Point(300, {Radio(2, 255, 40)})},
			TestWindow());
		assert(Summary.data.min_wifi_temp_2_4G == 20);
		assert(Summary.data.max_wifi_temp_2_4G == 30);
		AssertNear(*Summary.data.avg_wifi_temp_2_4G, 25.0);
		assert(Summary.data.latest_wifi_temp_2_4G == 30);
	}

	void TestLatestSelectionIsDeterministic() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(2, 20)}, "a"),
			 Point(300, {Radio(2, 255)}, "invalid-newer"),
			 Point(200, {Radio(2, 30)}, "b"),
			 Point(200, {Radio(2, 31)}, "c"),
			 Point(200, {Radio(2, 32), Radio(2, 33)}, "c")},
			TestWindow());
		assert(Summary.data.latest_wifi_temp_2_4G == 33);
	}

	void TestNegativeAverageRounding() {
		auto Summary = MCP::CalculateRadioTemperatureSummary(
			{Point(100, {Radio(5, -10)}), Point(200, {Radio(5, -11)}),
			 Point(300, {Radio(5, -11)})},
			TestWindow());
		AssertNear(*Summary.data.avg_wifi_temp_5G, -10.67);
	}

	void TestHalfOpenWindowFiltering() {
		std::vector<AnalyticsObjects::DeviceTimePoint> Records{
			Point(999, {Radio(2, 10)}), Point(1000, {Radio(2, 20)}),
			Point(1999, {Radio(2, 30)}), Point(2000, {Radio(2, 40)})};
		std::vector<AnalyticsObjects::DeviceTimePoint> Filtered;
		for (const auto &Record : Records) {
			if (MCP::TimestampInHalfOpenWindow(Record.timestamp, TestWindow()))
				Filtered.push_back(Record);
		}

		auto Summary = MCP::CalculateRadioTemperatureSummary(Filtered, TestWindow());
		assert(Summary.data.min_wifi_temp_2_4G == 20);
		assert(Summary.data.max_wifi_temp_2_4G == 30);
		assert(Summary.data.latest_wifi_temp_2_4G == 30);
		assert(Summary.meta.observedWindow.startTime == "1970-01-01T00:16:40Z");
		assert(Summary.meta.observedWindow.endTime == "1970-01-01T00:33:19Z");
	}

	void TestCutoverValidation() {
		MCP::Error E;
		uint64_t Cutover = 0;
		assert(MCP::ResolveTemperatureMigrationCutover("2026-07-01T00:00:00Z", "", Cutover));
		assert(Cutover == 1782864000);

		uint64_t EnvCutover = 0;
		assert(MCP::ResolveTemperatureMigrationCutover(
			"2026-07-01T00:00:00Z", "2026-08-01T00:00:00Z", EnvCutover));
		assert(EnvCutover == 1785542400);

		uint64_t FallbackCutover = 0;
		assert(!MCP::ResolveTemperatureMigrationCutover("not-a-timestamp", "",
														FallbackCutover));
		assert(FallbackCutover == 1782864000);

		MCP::Window Before;
		Before.startTime = Cutover - 1;
		Before.endTime = Cutover + 3600;
		Before.lookbackHours = 1;
		assert(!MCP::ValidateTemperatureCutover(Before, Cutover, E));
		assert(E.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
		assert(E.error == "temperature_range_before_cutover");

		MCP::Window At;
		At.startTime = Cutover;
		At.endTime = Cutover + 3600;
		At.lookbackHours = 1;
		assert(MCP::ValidateTemperatureCutover(At, Cutover, E));
	}

	Poco::URI::QueryParameters Params(const std::string &Query) {
		Poco::URI URI("http://example.test/?" + Query);
		return URI.getQueryParameters();
	}

	void TestRepresentativeSharedValidation() {
		MCP::Error E;
		std::string Token;
		assert(!MCP::ExtractBearerToken(std::nullopt, Token, E));
		assert(E.status == Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED);
		assert(E.error == "unauthorized");

		MCP::Window W;
		assert(!MCP::ValidateWindowQuery(
			Params("timestampTill=2026-08-21T06:25:00Z&lookbackHours=0"), 2000000000, 300,
			W, E));
		assert(E.error == "invalid_lookback_hours");
	}

} // namespace

int main() {
	TestTwoBandAggregation();
	TestResponseEnvelopeSerialization();
	TestNoSamplesAndUnsupportedBands();
	TestTemperatureValidityBounds();
	TestWifiTempPrecedenceAndFallback();
	TestLatestSelectionIsDeterministic();
	TestNegativeAverageRounding();
	TestHalfOpenWindowFiltering();
	TestCutoverValidation();
	TestRepresentativeSharedValidation();
	std::cout << "test_mcp_radio_temperature passed\n";
	return 0;
}
