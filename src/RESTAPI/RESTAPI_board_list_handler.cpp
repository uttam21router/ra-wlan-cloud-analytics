//
// Created by stephane bourque on 2022-03-11.
//

#include "RESTAPI_board_list_handler.h"
#include "RESTAPI/RESTAPI_analytics_db_helpers.h"
#include "StorageService.h"

namespace OpenWifi {
	void RESTAPI_board_list_handler::DoGet() {
		auto forVenue = GetParameter("forVenue", "");

		if (!forVenue.empty()) {
			std::vector<AnalyticsObjects::BoardInfo> Boards;
			auto F = [&](const AnalyticsObjects::BoardInfo &B) -> bool {
				if (B.venue.id == forVenue) {
					Boards.emplace_back(B);
				}
				return true;
			};
			DB_.Iterate(F);
			return ReturnObject("boards", Boards);
		}

		return ListHandler<BoardsDB>("boards", DB_, *this);
	}

} // namespace OpenWifi
