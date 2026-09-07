//
// Created for MCP gateway Wi-Fi temperature summary.
//

#pragma once

#include "StorageService.h"
#include "framework/RESTAPI_Handler.h"

namespace OpenWifi {

	class RESTAPI_gateway_wifi_temp_handler : public RESTAPIHandler {
	  public:
		RESTAPI_gateway_wifi_temp_handler(const RESTAPIHandler::BindingMap &bindings,
										  Poco::Logger &L, RESTAPI_GenericServerAccounting &Server,
										  uint64_t TransactionId, bool Internal)
			: RESTAPIHandler(bindings, L,
							 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_GET,
													  Poco::Net::HTTPRequest::HTTP_OPTIONS},
							 Server, TransactionId, Internal) {}

		static auto PathName() {
			return std::list<std::string>{
				"/api/v1/devices/{routerId}/radio-temperature-summary"};
		};

		const Poco::URI::QueryParameters &QueryParameters() const { return Parameters_; }

	  private:
		void DoGet() final;
		void DoPost() final{};
		void DoPut() final{};
		void DoDelete() final{};
	};
} // namespace OpenWifi
