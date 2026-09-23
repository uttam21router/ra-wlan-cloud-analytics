#include "storage_rssi_parser.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <utility>

namespace OpenWifi::Storage {
	bool ParseSsidDataForRssi(const std::string &Json, const std::string &RecordId,
							  Poco::Logger &Logger,
							  std::vector<AnalyticsObjects::SSIDTimePoint> &SSIDs) {
		SSIDs.clear();
		if (Json.empty())
			return true;
		try {
			Poco::JSON::Parser Parser;
			auto Array = Parser.parse(Json).extract<Poco::JSON::Array::Ptr>();
			for (auto const &Item : *Array) {
				try {
					auto Object = Item.extract<Poco::JSON::Object::Ptr>();
					if (Object->isArray("associations") && !Object->isNull("associations")) {
						auto Associations = Object->getArray("associations");
						Poco::JSON::Array::Ptr CleanAssociations = new Poco::JSON::Array;
						for (auto const &AssociationItem : *Associations) {
							try {
								auto AssociationObject =
									AssociationItem.extract<Poco::JSON::Object::Ptr>();
								AnalyticsObjects::UETimePoint UE;
								if (UE.from_json(AssociationObject)) {
									CleanAssociations->add(AssociationObject);
								} else {
									Logger.warning("Skipping malformed association item in timepoint id=" +
												   RecordId);
								}
							} catch (const Poco::Exception &E) {
								Logger.warning("Skipping malformed association item in timepoint id=" +
											   RecordId + ": " + E.displayText());
							} catch (...) {
								Logger.warning("Skipping malformed association item in timepoint id=" +
											   RecordId);
							}
						}
						Object->set("associations", CleanAssociations);
					}
					AnalyticsObjects::SSIDTimePoint SSID;
					if (SSID.from_json(Object)) {
						SSIDs.emplace_back(std::move(SSID));
					} else {
						Logger.warning("Skipping malformed SSID item in timepoint id=" + RecordId);
					}
				} catch (const Poco::Exception &E) {
					Logger.warning("Skipping malformed SSID item in timepoint id=" + RecordId +
								   ": " + E.displayText());
				} catch (...) {
					Logger.warning("Skipping malformed SSID item in timepoint id=" + RecordId);
				}
			}
			return true;
		} catch (const Poco::Exception &E) {
			Logger.warning("Skipping malformed ssid_data in timepoint id=" + RecordId +
						   ": " + E.displayText());
		} catch (...) {
			Logger.warning("Skipping malformed ssid_data in timepoint id=" + RecordId);
		}
		SSIDs.clear();
		return false;
	}
} // namespace OpenWifi::Storage
