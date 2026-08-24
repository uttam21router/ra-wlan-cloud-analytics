#pragma once

#include "RESTObjects/RESTAPI_AnalyticsObjects.h"
#include <Poco/Net/HTTPResponse.h>
#include <string>
#include <vector>

namespace OpenWifi {

	class RESTAPIHandler;

	class RouterIdResolver {
	  public:
		struct Result {
			std::string routerId;
			std::string resolvedBoardId;
			std::string resolvedVenueId;
			AnalyticsObjects::BoardInfo board;
		};

		struct Error {
			Poco::Net::HTTPResponse::HTTPStatus status =
				Poco::Net::HTTPResponse::HTTP_NOT_FOUND;
			std::string error = "not_found";
			std::string message = "Router was not found";
		};

		bool Resolve(RESTAPIHandler &Client, const std::string &routerId, Result &Resolved,
					 Error &E);
		static bool ClassifyProvisioningFailure(Poco::Net::HTTPResponse::HTTPStatus Status,
												Error &E);
		static bool ResolveBoardForVenue(const std::string &venueId,
										 const std::vector<AnalyticsObjects::BoardInfo> &Boards,
										 Result &Resolved, Error &E);
	};

	inline bool RouterIdResolver::ClassifyProvisioningFailure(
		Poco::Net::HTTPResponse::HTTPStatus Status, Error &E) {
		if (Status == Poco::Net::HTTPResponse::HTTP_NOT_FOUND ||
			Status == Poco::Net::HTTPResponse::HTTP_FORBIDDEN ||
			Status == Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED) {
			E.status = Poco::Net::HTTPResponse::HTTP_NOT_FOUND;
			E.error = "not_found";
			E.message = "Router was not found";
			return false;
		}
		E.status = Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY;
		E.error = "owprov_unavailable";
		E.message = "OWPROV was unavailable or returned an invalid response";
		return false;
	}

	inline bool RouterIdResolver::ResolveBoardForVenue(
		const std::string &venueId, const std::vector<AnalyticsObjects::BoardInfo> &Boards,
		Result &Resolved, Error &E) {
		std::vector<AnalyticsObjects::BoardInfo> MatchingBoards;
		for (const auto &Board : Boards) {
			for (const auto &Venue : Board.venueList) {
				if (Venue.id == venueId) {
					MatchingBoards.emplace_back(Board);
					break;
				}
			}
		}

		if (MatchingBoards.empty()) {
			E.status = Poco::Net::HTTPResponse::HTTP_NOT_FOUND;
			E.error = "not_found";
			E.message = "Router was not found";
			return false;
		}
		if (MatchingBoards.size() > 1) {
			E.status = Poco::Net::HTTPResponse::HTTP_CONFLICT;
			E.error = "multiple_boards";
			E.message = "Multiple Analytics boards match the requested router";
			return false;
		}

		Resolved.resolvedVenueId = venueId;
		Resolved.board = MatchingBoards.front();
		Resolved.resolvedBoardId = Resolved.board.info.id;
		return true;
	}

} // namespace OpenWifi
