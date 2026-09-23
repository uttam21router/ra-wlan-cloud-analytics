//
// Created for RSSI-specific persisted telemetry parsing.
//

#pragma once

#include "RESTObjects/RESTAPI_AnalyticsObjects.h"
#include <Poco/Logger.h>
#include <string>
#include <vector>

namespace OpenWifi::Storage {
	bool ParseSsidDataForRssi(const std::string &Json, const std::string &RecordId,
							  Poco::Logger &Logger,
							  std::vector<AnalyticsObjects::SSIDTimePoint> &SSIDs);
}
