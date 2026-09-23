//
// Created for RSSI-specific persisted telemetry parsing.
//

#pragma once

#include "RESTObjects/RESTAPI_AnalyticsObjects.h"
#include <Poco/Logger.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace OpenWifi::Storage {
	bool RssiSsidRecordCountExceedsLimit(std::size_t RawRecordCount, uint64_t MaxRecords,
										 bool *LimitExceeded);

	bool ParseSsidDataForRssi(const std::string &Json, const std::string &RecordId,
							  Poco::Logger &Logger,
							  std::vector<AnalyticsObjects::SSIDTimePoint> &SSIDs);
}
