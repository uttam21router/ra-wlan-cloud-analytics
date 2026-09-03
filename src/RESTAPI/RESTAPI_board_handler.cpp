//
// Created by stephane bourque on 2022-03-11.
//

#include "RESTAPI_board_handler.h"
#include "VenueCoordinator.h"

namespace OpenWifi {
	namespace {
		bool HasExactlyOneVenue(const AnalyticsObjects::BoardInfo &Board) {
			return Board.venueList.size() == 1 && !Board.venueList[0].id.empty();
		}
	} // namespace

	void RESTAPI_board_handler::DoGet() {
		auto id = GetBinding("id", "");
		if (id.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}

		AnalyticsObjects::BoardInfo B;
		if (!StorageService()->BoardsDB().GetRecord("id", id, B)) {
			return NotFound();
		}

		Poco::JSON::Object Answer;
		B.to_json(Answer);
		return ReturnObject(Answer);
	}

	void RESTAPI_board_handler::DoDelete() {
		auto id = GetBinding("id", "");
		if (id.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}

		AnalyticsObjects::BoardInfo B;
		if (!StorageService()->BoardsDB().GetRecord("id", id, B)) {
			return NotFound();
		}
		VenueCoordinator()->StopBoard(id);
		if (!StorageService()->BoardVenuesDB().DeleteBoard(id))
			return InternalError(RESTAPI::Errors::CouldNotBeDeleted);
		StorageService()->BoardsDB().DeleteRecord("id", id);
		StorageService()->TimePointsDB().DeleteBoard(id);
		return OK();
	}

	void RESTAPI_board_handler::DoPost() {
		auto id = GetBinding("id", "");
		if (id.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}

		const auto &RawObject = ParsedBody_;
		AnalyticsObjects::BoardInfo NewObject;
		if (!NewObject.from_json(RawObject)) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}
		if (!HasExactlyOneVenue(NewObject)) {
			return BadRequest(RESTAPI::Errors::VenueMustExist);
		}

		ProvObjects::CreateObjectInfo(RawObject, UserInfo_.userinfo, NewObject.info);

		bool VenueConflict = false;
		if (!StorageService()->BoardVenuesDB().CanAssignBoardVenues(NewObject, VenueConflict))
			return InternalError(RESTAPI::Errors::RecordNotCreated);
		if (VenueConflict)
			return InternalError(RESTAPI::Errors::RecordNotCreated);

		if (StorageService()->BoardsDB().CreateRecord(NewObject)) {
			if (!StorageService()->BoardVenuesDB().ReplaceBoardVenues(NewObject)) {
				StorageService()->BoardsDB().DeleteRecord("id", NewObject.info.id);
				return InternalError(RESTAPI::Errors::RecordNotCreated);
			}
			VenueCoordinator()->AddBoard(NewObject.info.id);
			AnalyticsObjects::BoardInfo NewBoard;
			StorageService()->BoardsDB().GetRecord("id", NewObject.info.id, NewBoard);
			Poco::JSON::Object Answer;
			NewBoard.to_json(Answer);
			return ReturnObject(Answer);
		}
		return InternalError(RESTAPI::Errors::RecordNotCreated);
	}

	void RESTAPI_board_handler::DoPut() {
		auto id = GetBinding("id", "");
		if (id.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}

		AnalyticsObjects::BoardInfo Existing;
		if (!StorageService()->BoardsDB().GetRecord("id", id, Existing)) {
			return NotFound();
		}

		const auto &RawObject = ParsedBody_;
		AnalyticsObjects::BoardInfo NewObject;
		if (!NewObject.from_json(RawObject)) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		ProvObjects::UpdateObjectInfo(RawObject, UserInfo_.userinfo, Existing.info);

		if (RawObject->has("venueList")) {
			if (!HasExactlyOneVenue(NewObject)) {
				return BadRequest(RESTAPI::Errors::VenueMustExist);
			}
			Existing.venueList = NewObject.venueList;
		}

		bool VenueConflict = false;
		if (!StorageService()->BoardVenuesDB().CanAssignBoardVenues(Existing, VenueConflict))
			return InternalError(RESTAPI::Errors::RecordNotUpdated);
		if (VenueConflict)
			return InternalError(RESTAPI::Errors::RecordNotUpdated);

		if (StorageService()->BoardsDB().UpdateRecord("id", Existing.info.id, Existing)) {
			if (!StorageService()->BoardVenuesDB().ReplaceBoardVenues(Existing))
				return InternalError(RESTAPI::Errors::RecordNotUpdated);
			VenueCoordinator()->UpdateBoard(Existing.info.id);
			AnalyticsObjects::BoardInfo NewBoard;
			StorageService()->BoardsDB().GetRecord("id", Existing.info.id, NewBoard);
			Poco::JSON::Object Answer;
			NewBoard.to_json(Answer);
			return ReturnObject(Answer);
		}
		return InternalError(RESTAPI::Errors::RecordNotUpdated);
	}
} // namespace OpenWifi
