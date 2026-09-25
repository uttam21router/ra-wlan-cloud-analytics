#pragma once

#include "framework/orm.h"
#include <optional>

namespace OpenWifi {
	struct SystemProperty {
		std::string property_key;
		std::string property_value;
		uint64_t created = 0;
		uint64_t modified = 0;
	};

	typedef Poco::Tuple<std::string, std::string, uint64_t, uint64_t>
		SystemPropertyDBRecordType;

	class SystemPropertiesDB : public ORM::DB<SystemPropertyDBRecordType, SystemProperty> {
	  public:
		static constexpr const char *AvailabilityValidFromKey = "availability_valid_from";

		SystemPropertiesDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L);

		bool GetAvailabilityValidFrom(uint64_t &validFrom);
		bool InitializeAvailabilityValidFrom(uint64_t seedValidFrom,
											 const std::optional<uint64_t> &configuredValidFrom);

	  private:
		bool Upgrade(uint32_t from, uint32_t &to) override;
		bool ParseAvailabilityValidFrom(const SystemProperty &property, uint64_t &validFrom);
		bool ValidateConfiguredSeed(const std::optional<uint64_t> &configuredValidFrom,
									 uint64_t persistedValidFrom);
	};
} // namespace OpenWifi
