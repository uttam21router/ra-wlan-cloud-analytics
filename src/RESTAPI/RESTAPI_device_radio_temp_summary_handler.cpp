#include "RESTAPI_device_radio_temp_summary_handler.h"

#include "RESTAPI_mcp_helpers.h"
#include "RouterIdResolver.h"
#include "StorageService.h"
#include "framework/MicroServiceFuncs.h"

#include <cstdlib>

namespace OpenWifi {

	namespace {
		MCP::Error ConvertResolverError(const RouterIdResolver::Error &ResolverError) {
			MCP::Error E;
			E.status = ResolverError.status;
			E.error = ResolverError.error;
			E.message = ResolverError.message;
			return E;
		}
	} // namespace

	void RESTAPI_device_radio_temp_summary_handler::DoGet() {
		MCP::Error Error;
		if (!MCP::AuthenticateBearerToken(*this, Error))
			return MCP::SendError(*this, Error);

		auto routerId = GetBinding("routerId", "");
		if (!MCP::ValidateRouterId(routerId, Error))
			return MCP::SendError(*this, Error);

		MCP::Window Window;
		auto ClockSkewSeconds = MicroServiceConfigGetInt("allowed.clock_skew.seconds", 300);
		if (!MCP::ValidateWindowQuery(Parameters_, Utils::Now(), ClockSkewSeconds, Window, Error))
			return MCP::SendError(*this, Error);

		RouterIdResolver Resolver;
		RouterIdResolver::Result Resolved;
		RouterIdResolver::Error ResolverError;
		if (!Resolver.Resolve(*this, routerId, Resolved, ResolverError)) {
			if (ResolverError.status == Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY) {
				poco_warning(Logger(), "Failed to resolve routerId through OWPROV");
			}
			return MCP::SendError(*this, ConvertResolverError(ResolverError));
		}

		if (!MCP::AuthorizeGatewayMetricsRead(UserInfo_, Resolved.resolvedBoardId,
											 Resolved.resolvedVenueId, Error))
			return MCP::SendError(*this, Error);

		uint64_t RetentionSeconds = 0;
		if (!MCP::FindVenueRetention(Resolved.board, Resolved.resolvedVenueId, RetentionSeconds)) {
			MCP::SetError(Error, Poco::Net::HTTPResponse::HTTP_NOT_FOUND, "not_found",
						  "Router was not found");
			return MCP::SendError(*this, Error);
		}
		if (!MCP::ValidateRetention(Window, RetentionSeconds, Utils::Now(), ClockSkewSeconds, Error))
			return MCP::SendError(*this, Error);

		const auto ConfiguredCutover = MicroServiceConfigGetString(
			"temperature.migration_cutover_time", "2026-07-01T00:00:00Z");
		const char *EnvCutover = std::getenv("TEMPERATURE_MIGRATION_CUTOVER_TIME");
		uint64_t CutoverTime = 0;
		if (!MCP::ResolveTemperatureMigrationCutover(
				ConfiguredCutover, EnvCutover == nullptr ? std::string() : std::string(EnvCutover),
				CutoverTime)) {
			poco_warning(Logger(),
						 "Invalid temperature migration cutover; using safe default");
		}
		if (!MCP::ValidateTemperatureCutover(Window, CutoverTime, Error))
			return MCP::SendError(*this, Error);

		std::vector<AnalyticsObjects::DeviceTimePoint> Records;
		if (!StorageService()->TimePointsDB().SelectRadioRecordsBySerial(
				Resolved.resolvedBoardId, routerId, Window.startTime, Window.endTime, Records)) {
			poco_error(Logger(), "Failed to query timepoints for radio temperature summary");
			MCP::SetError(Error, Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY, "storage_unavailable",
						  "Analytics storage was unavailable");
			return MCP::SendError(*this, Error);
		}

		auto Summary = MCP::CalculateRadioTemperatureSummary(Records, Window);
		return Object(Summary);
	}

} // namespace OpenWifi
