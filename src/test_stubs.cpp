#if __has_include("Poco/JSON/Object.h")
#include "RESTAPI/RESTAPI_mcp_helpers.h"
#include "framework/AuthClient.h"
#include "framework/MicroServiceFuncs.h"
#include "RouterIdResolver.h"
#include "StorageService.h"
#include "VenueCoordinator.h"

namespace OpenWifi {

	uint64_t MicroServiceConfigGetInt([[maybe_unused]] const std::string &key, uint64_t defaultVal) {
		if (key == "allowed.clock_skew.seconds") return 300;
		if (key == "monitoring.duration") return 365 * 24 * 3600;
		return defaultVal;
	}

	std::string MicroServiceConfigGetString([[maybe_unused]] const std::string &key, const std::string &defaultVal) {
		return defaultVal;
	}

	std::string MicroServiceConfigPath([[maybe_unused]] const std::string &key, const std::string &defaultVal) {
		return defaultVal;
	}

	std::string MicroServiceCreateUUID() {
		return "12345678-1234-1234-1234-123456789012";
	}

	Types::MicroServiceMetaVec MicroServiceGetServices([[maybe_unused]] const std::string &Type) {
		return {};
	}

	bool AllowExternalMicroServices() {
		return false;
	}

	const std::string &MicroServiceDataDirectory() {
		static const std::string dir = "/tmp";
		return dir;
	}

	std::string MicroServicePublicEndPoint() {
		return "http://localhost";
	}

	bool AuthClient::IsAuthorized([[maybe_unused]] const std::string &SessionToken, [[maybe_unused]] SecurityObjects::UserInfoAndPolicy &UInfo,
								  [[maybe_unused]] std::uint64_t TID, bool &Expired, bool &Contacted, [[maybe_unused]] bool Sub) {
		Expired = false;
		Contacted = true;
		return true;
	}

	void VenueCoordinator::GetDevices([[maybe_unused]] std::string &venueId, [[maybe_unused]] AnalyticsObjects::DeviceInfoList &devices) {
	}

} // namespace OpenWifi
#endif
