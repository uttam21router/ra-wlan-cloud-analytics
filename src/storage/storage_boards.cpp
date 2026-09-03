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
#include <set>

namespace OpenWifi {

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

	bool BoardsDB::Upgrade(uint32_t from, uint32_t &to) {
		std::vector<std::string> Statements{};
		RunScript(Statements);
		to = from;
		return true;
	}

	bool BoardsDB::FindBoardsByVenue(const std::string &venueId,
									 std::vector<AnalyticsObjects::BoardInfo> &boards) {
		boards.clear();
		if (venueId.empty())
			return true;

		return Iterate([&](const AnalyticsObjects::BoardInfo &Board) {
			if (Board.venueList.size() == 1 &&
				!Board.venueList[0].id.empty() &&
				Board.venueList[0].id == venueId) {
				boards.emplace_back(Board);
			}
			return true;
		});
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
	Out.venueList.clear();
	if (!In.get<6>().empty()) {
		OpenWifi::AnalyticsObjects::VenueInfo Venue;
		Venue.id = In.get<6>();
		Venue.name = In.get<7>();
		Venue.description = In.get<8>();
		Venue.retention = In.get<9>();
		Venue.interval = In.get<10>();
		Venue.monitorSubVenues = In.get<11>();
		Out.venueList.emplace_back(Venue);
	}
}

template <>
void ORM::DB<OpenWifi::BoardDBRecordType, OpenWifi::AnalyticsObjects::BoardInfo>::Convert(
	const OpenWifi::AnalyticsObjects::BoardInfo &In, OpenWifi::BoardDBRecordType &Out) {
	OpenWifi::AnalyticsObjects::VenueInfo Venue;
	if (!In.venueList.empty()) {
		Venue = In.venueList[0];
	}

	Out.set<0>(In.info.id);
	Out.set<1>(In.info.name);
	Out.set<2>(In.info.description);
	Out.set<3>(OpenWifi::RESTAPI_utils::to_string(In.info.notes));
	Out.set<4>(In.info.created);
	Out.set<5>(In.info.modified);
	Out.set<6>(Venue.id);
	Out.set<7>(Venue.name);
	Out.set<8>(Venue.description);
	Out.set<9>(Venue.retention);
	Out.set<10>(Venue.interval);
	Out.set<11>(Venue.monitorSubVenues);
}
