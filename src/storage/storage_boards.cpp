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

	static AnalyticsObjects::VenueInfo FirstVenueFromLegacyVenueList(const std::string &In) {
		auto Venues = RESTAPI_utils::to_object_array<AnalyticsObjects::VenueInfo>(In);
		if (Venues.empty())
			return {};
		return Venues[0];
	}

	static void BackfillVenueField(Poco::Data::Session &Session, const std::string &FieldName,
								   bool FieldIsList) {
		typedef Poco::Tuple<std::string, std::string, std::string> LegacyRecord;
		std::vector<LegacyRecord> Records;
		Poco::Data::Statement Select(Session);
		Select << "SELECT id, venueid, " + FieldName + " FROM boards",
			Poco::Data::Keywords::into(Records);
		Select.execute();

		for (const auto &Record : Records) {
			auto Id = Record.get<0>();
			auto ExistingVenueId = Record.get<1>();
			auto LegacyVenue = Record.get<2>();
			if (!ExistingVenueId.empty() || LegacyVenue.empty())
				continue;

			auto Venue = FieldIsList ? FirstVenueFromLegacyVenueList(LegacyVenue)
									 : VenueFromString(LegacyVenue);
			if (Venue.id.empty())
				continue;

			Poco::Data::Statement Update(Session);
			Update << "UPDATE boards SET venueid=?, venuename=?, venuedescription=?, "
					  "retention=?, interval=?, monitorsubvenues=? WHERE id=?",
				Poco::Data::Keywords::use(Venue.id), Poco::Data::Keywords::use(Venue.name),
				Poco::Data::Keywords::use(Venue.description),
				Poco::Data::Keywords::use(Venue.retention),
				Poco::Data::Keywords::use(Venue.interval),
				Poco::Data::Keywords::use(Venue.monitorSubVenues), Poco::Data::Keywords::use(Id);
			Update.execute();
		}
	}

	static ORM::FieldVec Boards_Fields{// object info
									   ORM::Field{"id", 64, true},
									   ORM::Field{"name", ORM::FieldType::FT_TEXT},
									   ORM::Field{"description", ORM::FieldType::FT_TEXT},
									   ORM::Field{"notes", ORM::FieldType::FT_TEXT},
									   ORM::Field{"created", ORM::FieldType::FT_BIGINT},
									   ORM::Field{"modified", ORM::FieldType::FT_BIGINT},
									   ORM::Field{"venueId", ORM::FieldType::FT_TEXT},
									   ORM::Field{"venueName", ORM::FieldType::FT_TEXT},
									   ORM::Field{"venueDescription", ORM::FieldType::FT_TEXT},
									   ORM::Field{"retention", ORM::FieldType::FT_BIGINT},
									   ORM::Field{"interval", ORM::FieldType::FT_BIGINT},
									   ORM::Field{"monitorSubVenues", ORM::FieldType::FT_BOOLEAN}};

	static ORM::IndexVec BoardsDB_Indexes{
		{std::string("boards_name_index"),
		 ORM::IndexEntryVec{{std::string("name"), ORM::Indextype::ASC}}}};

	BoardsDB::BoardsDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L)
		: DB(T, "boards", Boards_Fields, BoardsDB_Indexes, P, L, "bor") {}

	bool BoardsDB::Upgrade([[maybe_unused]] uint32_t from, uint32_t &to) {
		std::vector<std::string> Statements{
			"ALTER TABLE boards ADD COLUMN venueid TEXT",
			"ALTER TABLE boards ADD COLUMN venuename TEXT",
			"ALTER TABLE boards ADD COLUMN venuedescription TEXT",
			"ALTER TABLE boards ADD COLUMN retention BIGINT",
			"ALTER TABLE boards ADD COLUMN interval BIGINT",
			"ALTER TABLE boards ADD COLUMN monitorsubvenues BOOLEAN",
			"UPDATE boards SET venueid='' WHERE venueid IS NULL",
			"UPDATE boards SET venuename='' WHERE venuename IS NULL",
			"UPDATE boards SET venuedescription='' WHERE venuedescription IS NULL",
			"UPDATE boards SET retention=0 WHERE retention IS NULL",
			"UPDATE boards SET interval=0 WHERE interval IS NULL",
			"UPDATE boards SET monitorsubvenues=false WHERE monitorsubvenues IS NULL"};
		RunScript(Statements);

		try {
			Poco::Data::Session Session = Pool_.get();
			BackfillVenueField(Session, "venue", false);
		} catch (...) {
		}
		try {
			Poco::Data::Session Session = Pool_.get();
			auto LegacyVenueListField = std::string("venue") + "List";
			BackfillVenueField(Session, LegacyVenueListField, true);
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
	Out.venue.id = In.get<6>();
	Out.venue.name = In.get<7>();
	Out.venue.description = In.get<8>();
	Out.venue.retention = In.get<9>();
	Out.venue.interval = In.get<10>();
	Out.venue.monitorSubVenues = In.get<11>();
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
	Out.set<6>(In.venue.id);
	Out.set<7>(In.venue.name);
	Out.set<8>(In.venue.description);
	Out.set<9>(In.venue.retention);
	Out.set<10>(In.venue.interval);
	Out.set<11>(In.venue.monitorSubVenues);
}
