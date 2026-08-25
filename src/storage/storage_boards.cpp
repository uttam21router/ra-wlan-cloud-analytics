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
									   ORM::Field{"venueList", ORM::FieldType::FT_TEXT}};

	static ORM::IndexVec BoardsDB_Indexes{
		{std::string("boards_name_index"),
		 ORM::IndexEntryVec{{std::string("name"), ORM::Indextype::ASC}}}};

	static ORM::FieldVec BoardVenue_Fields{ORM::Field{"board_id", 64, false},
										   ORM::Field{"venue_id", 64, true}};

	static ORM::IndexVec BoardVenueDB_Indexes{
		{std::string("board_venues_board_idx"),
		 ORM::IndexEntryVec{{std::string("board_id"), ORM::Indextype::ASC}}},
		{std::string("board_venues_venue_idx"),
		 ORM::IndexEntryVec{{std::string("venue_id"), ORM::Indextype::ASC}}}};

	BoardsDB::BoardsDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L)
		: DB(T, "boards", Boards_Fields, BoardsDB_Indexes, P, L, "bor") {}

	bool BoardsDB::Upgrade([[maybe_unused]] uint32_t from, uint32_t &to) {
		std::vector<std::string> Statements{};
		RunScript(Statements);
		to = 2;
		return true;
	}

	BoardVenueDB::BoardVenueDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L)
		: DB(T, "board_venues", BoardVenue_Fields, BoardVenueDB_Indexes, P, L, "bve") {}

	bool BoardVenueDB::Upgrade([[maybe_unused]] uint32_t from, uint32_t &to) {
		std::vector<std::string> Statements{};
		RunScript(Statements);
		to = 1;
		return true;
	}

	BoardVenueLookupResult BoardVenueDB::GetBoardIdByVenue(const std::string &venueId,
														   std::string &boardId) {
		boardId.clear();
		if (venueId.empty())
			return BoardVenueLookupResult::NotFound;

		try {
			Poco::Data::Session Session = Pool_.get();
			Poco::Data::Statement Select(Session);
			std::vector<std::string> BoardIds;
			auto VenueId = venueId;

			Select << ConvertParams("select board_id from " + TableName_ + " where venue_id=?"),
				Poco::Data::Keywords::into(BoardIds), Poco::Data::Keywords::use(VenueId);
			Select.execute();

			if (BoardIds.empty())
				return BoardVenueLookupResult::NotFound;

			boardId = BoardIds.front();
			return BoardVenueLookupResult::Found;
		} catch (const Poco::Exception &E) {
			Logger_.log(E);
		}
		return BoardVenueLookupResult::StorageError;
	}

	bool BoardVenueDB::CanAssignBoardVenues(const AnalyticsObjects::BoardInfo &Board,
											bool &Conflict) {
		Conflict = false;
		std::set<std::string> SeenVenues;
		for (const auto &Venue : Board.venueList) {
			if (Venue.id.empty() || !SeenVenues.insert(Venue.id).second)
				continue;

			std::string ExistingBoardId;
			auto Result = GetBoardIdByVenue(Venue.id, ExistingBoardId);
			if (Result == BoardVenueLookupResult::StorageError)
				return false;
			if (Result == BoardVenueLookupResult::Found && ExistingBoardId != Board.info.id) {
				Conflict = true;
				return true;
			}
		}
		return true;
	}

	bool BoardVenueDB::ReplaceBoardVenues(const AnalyticsObjects::BoardInfo &Board) {
		try {
			Poco::Data::Session Session = Pool_.get();
			Session.begin();

			Poco::Data::Statement Delete(Session);
			auto BoardId = Board.info.id;
			Delete << ConvertParams("delete from " + TableName_ + " where board_id=?"),
				Poco::Data::Keywords::use(BoardId);
			Delete.execute();

			std::set<std::string> SeenVenues;
			for (const auto &Venue : Board.venueList) {
				if (Venue.id.empty() || !SeenVenues.insert(Venue.id).second)
					continue;

				Poco::Data::Statement Insert(Session);
				auto VenueId = Venue.id;
				Insert << ConvertParams("insert into " + TableName_ +
										 " ( board_id, venue_id ) values (?, ?)"),
					Poco::Data::Keywords::use(BoardId), Poco::Data::Keywords::use(VenueId);
				Insert.execute();
			}

			Session.commit();
			return true;
		} catch (const Poco::Exception &E) {
			Logger_.log(E);
		}
		return false;
	}

	bool BoardVenueDB::DeleteBoard(const std::string &boardId) {
		try {
			Poco::Data::Session Session = Pool_.get();
			Session.begin();

			Poco::Data::Statement Delete(Session);
			auto BoardId = boardId;
			Delete << ConvertParams("delete from " + TableName_ + " where board_id=?"),
				Poco::Data::Keywords::use(BoardId);
			Delete.execute();

			Session.commit();
			return true;
		} catch (const Poco::Exception &E) {
			Logger_.log(E);
		}
		return false;
	}

	bool BoardVenueDB::RebuildFromBoards(BoardsDB &Boards) {
		std::vector<AnalyticsObjects::BoardInfo> BoardList;
		auto Visitor = [&](const AnalyticsObjects::BoardInfo &Board) {
			BoardList.emplace_back(Board);
			return true;
		};
		if (!Boards.Iterate(Visitor))
			return false;

		try {
			Poco::Data::Session Session = Pool_.get();
			Session.begin();

			Poco::Data::Statement Clear(Session);
			Clear << "delete from " + TableName_;
			Clear.execute();

			std::set<std::string> SeenBoardVenuePairs;
			for (const auto &Board : BoardList) {
				for (const auto &Venue : Board.venueList) {
					if (Venue.id.empty())
						continue;

					auto Pair = Board.info.id + '\n' + Venue.id;
					if (!SeenBoardVenuePairs.insert(Pair).second)
						continue;

					Poco::Data::Statement Insert(Session);
					auto BoardId = Board.info.id;
					auto VenueId = Venue.id;
					Insert << ConvertParams("insert into " + TableName_ +
											 " ( board_id, venue_id ) values (?, ?)"),
						Poco::Data::Keywords::use(BoardId), Poco::Data::Keywords::use(VenueId);
					Insert.execute();
				}
			}

			Session.commit();
			return true;
		} catch (const Poco::Exception &E) {
			Logger_.log(E);
		}
		return false;
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
	Out.venueList = OpenWifi::RESTAPI_utils::to_object_array<OpenWifi::AnalyticsObjects::VenueInfo>(
		In.get<6>());
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
	Out.set<6>(OpenWifi::RESTAPI_utils::to_string(In.venueList));
}

template <>
void ORM::DB<OpenWifi::BoardVenueDBRecordType, OpenWifi::BoardVenueRecord>::Convert(
	const OpenWifi::BoardVenueDBRecordType &In, OpenWifi::BoardVenueRecord &Out) {
	Out.board_id = In.get<0>();
	Out.venue_id = In.get<1>();
}

template <>
void ORM::DB<OpenWifi::BoardVenueDBRecordType, OpenWifi::BoardVenueRecord>::Convert(
	const OpenWifi::BoardVenueRecord &In, OpenWifi::BoardVenueDBRecordType &Out) {
	Out.set<0>(In.board_id);
	Out.set<1>(In.venue_id);
}
