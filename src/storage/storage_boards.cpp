//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include "storage_boards.h"
#include "framework/OpenWifiTypes.h"
#include "framework/RESTAPI_utils.h"

namespace OpenWifi {

	static AnalyticsObjects::VenueInfo VenueFromString(const std::string &In) {
		AnalyticsObjects::VenueInfo Venue;
		if (In.empty())
			return Venue;

		try {
			Poco::JSON::Parser Parser;
			auto Object = Parser.parse(In).extract<Poco::JSON::Object::Ptr>();
			Venue.from_json(Object);
		} catch (...) {
		}
		return Venue;
	}

	static std::string FirstVenueFromLegacyVenueList(const std::string &In) {
		auto Venues = RESTAPI_utils::to_object_array<AnalyticsObjects::VenueInfo>(In);
		if (Venues.empty())
			return "";
		return RESTAPI_utils::to_string(Venues[0]);
	}

	static ORM::FieldVec Boards_Fields{// object info
									   ORM::Field{"id", 64, true},
									   ORM::Field{"name", ORM::FieldType::FT_TEXT},
									   ORM::Field{"description", ORM::FieldType::FT_TEXT},
									   ORM::Field{"notes", ORM::FieldType::FT_TEXT},
									   ORM::Field{"created", ORM::FieldType::FT_BIGINT},
									   ORM::Field{"modified", ORM::FieldType::FT_BIGINT},
									   ORM::Field{"venue", ORM::FieldType::FT_TEXT}};

	static ORM::IndexVec BoardsDB_Indexes{
		{std::string("boards_name_index"),
		 ORM::IndexEntryVec{{std::string("name"), ORM::Indextype::ASC}}}};

	BoardsDB::BoardsDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L)
		: DB(T, "boards", Boards_Fields, BoardsDB_Indexes, P, L, "bor") {}

	bool BoardsDB::Upgrade([[maybe_unused]] uint32_t from, uint32_t &to) {
		std::vector<std::string> Statements{"ALTER TABLE boards ADD COLUMN venue TEXT",
											"UPDATE boards SET venue='' WHERE venue IS NULL"};
		RunScript(Statements);

		try {
			typedef Poco::Tuple<std::string, std::string, std::string> LegacyRecord;
			std::vector<LegacyRecord> Records;
			Poco::Data::Session Session = Pool_.get();
			Poco::Data::Statement Select(Session);
			auto LegacyVenueListField = std::string("venue") + "List";
			Select << "SELECT id, venue, " + LegacyVenueListField + " FROM boards",
				Poco::Data::Keywords::into(Records);
			Select.execute();

			for (const auto &Record : Records) {
				auto Id = Record.get<0>();
				auto Venue = Record.get<1>();
				auto VenueList = Record.get<2>();
				if (Venue.empty() && !VenueList.empty()) {
					auto BackfilledVenue = FirstVenueFromLegacyVenueList(VenueList);
					if (!BackfilledVenue.empty()) {
						Poco::Data::Statement Update(Session);
						Update << "UPDATE boards SET venue=? WHERE id=?",
							Poco::Data::Keywords::use(BackfilledVenue),
							Poco::Data::Keywords::use(Id);
						Update.execute();
					}
				}
			}
		} catch (...) {
		}

		to = 2;
		return true;
	}
} // namespace OpenWifi

template <>
void ORM::DB<OpenWifi::BoardDBRecordType, OpenWifi::AnalyticsObjects::BoardInfo>::Convert(
	const OpenWifi::BoardDBRecordType &In, OpenWifi::AnalyticsObjects::BoardInfo &Out) {
	Out.info.id = In.get<0>();
	Out.info.name = In.get<1>();
	Out.info.description = In.get<2>();
	Out.info.notes =
		OpenWifi::RESTAPI_utils::to_object_array<OpenWifi::SecurityObjects::NoteInfo>(In.get<3>());
	Out.info.created = In.get<4>();
	Out.info.modified = In.get<5>();
	Out.venue = OpenWifi::VenueFromString(In.get<6>());
}

template <>
void ORM::DB<OpenWifi::BoardDBRecordType, OpenWifi::AnalyticsObjects::BoardInfo>::Convert(
	const OpenWifi::AnalyticsObjects::BoardInfo &In, OpenWifi::BoardDBRecordType &Out) {
	Out.set<0>(In.info.id);
	Out.set<1>(In.info.name);
	Out.set<2>(In.info.description);
	Out.set<3>(OpenWifi::RESTAPI_utils::to_string(In.info.notes));
	Out.set<4>(In.info.created);
	Out.set<5>(In.info.modified);
	Out.set<6>(OpenWifi::RESTAPI_utils::to_string(In.venue));
}
