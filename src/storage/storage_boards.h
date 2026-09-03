//
// Created by stephane bourque on 2022-03-11.
//

#pragma once

#include "RESTObjects/RESTAPI_AnalyticsObjects.h"
#include "framework/orm.h"

namespace OpenWifi {
	typedef Poco::Tuple<std::string, std::string, std::string, std::string, uint64_t, uint64_t,
						std::string>
		BoardDBRecordType;
	typedef Poco::Tuple<std::string, std::string> BoardVenueDBRecordType;

	struct BoardVenueRecord {
		std::string board_id;
		std::string venue_id;
	};

	enum class BoardVenueLookupResult {
		Found,
		NotFound,
		MultipleFound,
		StorageError,
	};

	class BoardsDB : public ORM::DB<BoardDBRecordType, AnalyticsObjects::BoardInfo> {
	  public:
		BoardsDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L);
		virtual ~BoardsDB(){};

	  private:
		bool Upgrade(uint32_t from, uint32_t &to) override;
	};

	class BoardVenueDB : public ORM::DB<BoardVenueDBRecordType, BoardVenueRecord> {
	  public:
		BoardVenueDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L);
		virtual ~BoardVenueDB(){};

		BoardVenueLookupResult GetBoardIdByVenue(const std::string &venueId,
												 std::string &boardId);
		bool CanAssignBoardVenues(const AnalyticsObjects::BoardInfo &Board, bool &Conflict);
		bool ReplaceBoardVenues(const AnalyticsObjects::BoardInfo &Board);
		bool DeleteBoard(const std::string &boardId);
		bool RebuildFromBoards(BoardsDB &Boards);

	  private:
		bool Upgrade(uint32_t from, uint32_t &to) override;
	};
} // namespace OpenWifi
