#include "storage_system_properties.h"

#include "RESTAPI/RESTAPI_mcp_helpers.h"
#include "framework/utils.h"

namespace OpenWifi {
	static ORM::FieldVec SystemProperties_Fields{
		ORM::Field{"property_key", 128, true},
		ORM::Field{"property_value", ORM::FieldType::FT_TEXT},
		ORM::Field{"created", ORM::FieldType::FT_BIGINT},
		ORM::Field{"modified", ORM::FieldType::FT_BIGINT}};

	SystemPropertiesDB::SystemPropertiesDB(OpenWifi::DBType T, Poco::Data::SessionPool &P,
										   Poco::Logger &L)
		: DB(T, "system_properties", SystemProperties_Fields, {}, P, L, "spr") {}

	bool SystemPropertiesDB::Upgrade([[maybe_unused]] uint32_t from, uint32_t &to) {
		to = 1;
		return true;
	}

	bool SystemPropertiesDB::ParseAvailabilityValidFrom(const SystemProperty &property,
												uint64_t &validFrom) {
		if (MCP::ParseTimestampTill(property.property_value, validFrom))
			return true;

		try {
			size_t ParsedChars = 0;
			auto Parsed = std::stoull(property.property_value, &ParsedChars);
			if (ParsedChars == property.property_value.size()) {
				validFrom = Parsed;
				return true;
			}
		} catch (...) {
		}

		Logger_.error("system_properties.availability_valid_from is invalid.");
		return false;
	}

	bool SystemPropertiesDB::ValidateConfiguredSeed(
		const std::optional<uint64_t> &configuredValidFrom, uint64_t persistedValidFrom) {
		if (configuredValidFrom && *configuredValidFrom != persistedValidFrom) {
			Logger_.error("availability_valid_from configuration mismatch: persisted "
						  "system_properties value is authoritative.");
			return false;
		}
		return true;
	}

	bool SystemPropertiesDB::GetAvailabilityValidFrom(uint64_t &validFrom) {
		SystemProperty Property;
		if (!GetRecord("property_key", AvailabilityValidFromKey, Property)) {
			Logger_.error("system_properties.availability_valid_from is not initialized.");
			return false;
		}
		return ParseAvailabilityValidFrom(Property, validFrom);
	}

	bool SystemPropertiesDB::InitializeAvailabilityValidFrom(
		uint64_t seedValidFrom, const std::optional<uint64_t> &configuredValidFrom) {
		SystemProperty Property;
		if (GetRecord("property_key", AvailabilityValidFromKey, Property)) {
			uint64_t PersistedValidFrom = 0;
			if (!ParseAvailabilityValidFrom(Property, PersistedValidFrom))
				return false;
			return ValidateConfiguredSeed(configuredValidFrom, PersistedValidFrom);
		}

		const auto Now = Utils::Now();
		Property.property_key = AvailabilityValidFromKey;
		Property.property_value = MCP::FormatTimestamp(seedValidFrom);
		Property.created = Now;
		Property.modified = Now;

		if (CreateRecord(Property))
			return true;

		if (GetRecord("property_key", AvailabilityValidFromKey, Property)) {
			uint64_t PersistedValidFrom = 0;
			if (!ParseAvailabilityValidFrom(Property, PersistedValidFrom))
				return false;
			return ValidateConfiguredSeed(configuredValidFrom, PersistedValidFrom);
		}

		Logger_.error("Unable to initialize system_properties.availability_valid_from.");
		return false;
	}
} // namespace OpenWifi

template <>
void ORM::DB<OpenWifi::SystemPropertyDBRecordType, OpenWifi::SystemProperty>::Convert(
	const OpenWifi::SystemPropertyDBRecordType &In, OpenWifi::SystemProperty &Out) {
	Out.property_key = In.get<0>();
	Out.property_value = In.get<1>();
	Out.created = In.get<2>();
	Out.modified = In.get<3>();
}

template <>
void ORM::DB<OpenWifi::SystemPropertyDBRecordType, OpenWifi::SystemProperty>::Convert(
	const OpenWifi::SystemProperty &In, OpenWifi::SystemPropertyDBRecordType &Out) {
	Out.set<0>(In.property_key);
	Out.set<1>(In.property_value);
	Out.set<2>(In.created);
	Out.set<3>(In.modified);
}
