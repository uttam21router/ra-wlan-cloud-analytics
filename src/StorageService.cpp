//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include "StorageService.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"
#include "Poco/Data/Statement.h"
#include "Poco/Exception.h"
#include "fmt/format.h"
#include "framework/MicroServiceFuncs.h"
#include "framework/utils.h"

#include <map>
#include <vector>

namespace OpenWifi {

	int Storage::Start() {
		poco_notice(Logger(), "Starting...");
		std::lock_guard Guard(Mutex_);

		StorageClass::Start();

		BoardsDB_ = std::make_unique<OpenWifi::BoardsDB>(dbType_, *Pool_, Logger());
		TimePointsDB_ = std::make_unique<OpenWifi::TimePointDB>(dbType_, *Pool_, Logger());
		WifiClientHistoryDB_ =
			std::make_unique<OpenWifi::WifiClientHistoryDB>(dbType_, *Pool_, Logger());

		if (dbType_ == OpenWifi::DBType::pgsql) {
			if (!ValidatePostgreSQLSchema()) {
				const std::string Error =
					"Database schema is not compatible with this analytics version. "
					"Run Flyway migrations before starting the service.";
				poco_fatal(Logger(), Error);
				throw Poco::RuntimeException(Error);
			}
		} else {
			TimePointsDB_->Create();
			BoardsDB_->Create();
			WifiClientHistoryDB_->Create();
		}

		PeriodicCleanup_ = MicroServiceConfigGetInt("storage.cleanup.interval", 6 * 60 * 60);
		if (PeriodicCleanup_ < 1 * 60 * 60)
			PeriodicCleanup_ = 1 * 60 * 60;

		Updater_.start(*this);

		TimerCallback_ = std::make_unique<Poco::TimerCallback<Storage>>(*this, &Storage::onTimer);
		Timer_.setStartInterval(60 * 1000);						   // first run in 20 seconds
		Timer_.setPeriodicInterval((long)PeriodicCleanup_ * 1000); // 1 hours
		Timer_.start(*TimerCallback_);

		return 0;
	}

	bool Storage::ValidatePostgreSQLTable(const std::string &tableName) {
		try {
			Poco::Data::Session Session = Pool_->get();
			uint64_t Count = 0;
			const std::string Statement = fmt::format(
				"select count(*) from information_schema.tables "
				"where table_schema = current_schema() and table_name = '{}'",
				ORM::Escape(tableName));

			Session << Statement, Poco::Data::Keywords::into(Count), Poco::Data::Keywords::now;
			if (Count == 1)
				return true;

			poco_fatal(Logger(), fmt::format("Missing PostgreSQL schema table: {}", tableName));
		} catch (const Poco::Exception &E) {
			Logger().log(E);
		}
		return false;
	}

	bool Storage::ValidatePostgreSQLColumn(const std::string &tableName,
										   const std::string &columnName) {
		try {
			Poco::Data::Session Session = Pool_->get();
			uint64_t Count = 0;
			const std::string Statement = fmt::format(
				"select count(*) from information_schema.columns "
				"where table_schema = current_schema() and table_name = '{}' and column_name = '{}'",
				ORM::Escape(tableName), ORM::Escape(columnName));

			Session << Statement, Poco::Data::Keywords::into(Count), Poco::Data::Keywords::now;
			if (Count == 1)
				return true;

			poco_fatal(Logger(),
					   fmt::format("Missing PostgreSQL schema element: {}.{}", tableName,
								   columnName));
		} catch (const Poco::Exception &E) {
			Logger().log(E);
		}
		return false;
	}

	bool Storage::ValidatePostgreSQLSchema() {
		const std::map<std::string, std::vector<std::string>> RequiredColumns{
			{"boards", {"id", "name", "description", "notes", "created", "modified", "venuelist"}},
			{"timepoints",
			 {"id", "boardid", "timestamp", "ap_data", "ssid_data", "radio_data", "device_info",
			  "serialnumber"}},
			{"wificlienthistory",
			 {"timestamp", "station_id", "bssid", "ssid", "rssi", "rx_bitrate", "rx_chwidth",
			  "rx_mcs", "rx_nss", "rx_vht", "tx_bitrate", "tx_chwidth", "tx_mcs", "tx_nss",
			  "tx_vht", "rx_bytes", "tx_bytes", "rx_duration", "tx_duration", "rx_packets",
			  "tx_packets", "ipv4", "ipv6", "channel_width", "noise", "tx_power", "channel",
			  "active_ms", "busy_ms", "receive_ms", "mode", "ack_signal", "ack_signal_avg",
			  "connected", "inactive", "tx_retries", "venue_id"}},
			{"flyway_schema_history",
			 {"installed_rank", "version", "description", "type", "script", "checksum",
			  "installed_by", "installed_on", "execution_time", "success"}}};

		for (const auto &[tableName, columns] : RequiredColumns) {
			if (!ValidatePostgreSQLTable(tableName))
				return false;
			for (const auto &columnName : columns) {
				if (!ValidatePostgreSQLColumn(tableName, columnName))
					return false;
			}
		}
		return true;
	}

	void Storage::onTimer([[maybe_unused]] Poco::Timer &timer) {
		BoardsDB::RecordVec BoardList;
		uint64_t start = 0;
		bool done = false;
		const uint64_t batch = 100;
		poco_information(Logger(), "Starting cleanup of TimePoint Database");
		while (!done) {
			if (!BoardsDB().GetRecords(start, batch, BoardList)) {
				for (const auto &board : BoardList) {
					for (const auto &venue : board.venueList) {
						auto now = Utils::Now();
						auto lower_bound = now - venue.retention;
						poco_information(
							Logger(),
							fmt::format("Removing old records for board '{}'", board.info.name));
						BoardsDB().DeleteRecords(fmt::format(" boardId='{}' and timestamp<{}",
															 board.info.id, lower_bound));
					}
				}
			}
			done = (BoardList.size() < batch);
		}

		auto MaxDays = MicroServiceConfigGetInt("wificlient.age.limit", 14);
		auto LowerDate = Utils::Now() - (MaxDays * 60 * 60 * 24);
		poco_information(Logger(),
						 fmt::format("Removing WiFi Clients history older than {} days.", MaxDays));
		StorageService()->WifiClientHistoryDB().DeleteRecords(
			fmt::format(" timestamp<{} ", LowerDate));
		poco_information(Logger(), fmt::format("Done cleanup of databases. Next run in {} seconds.",
											   PeriodicCleanup_));
	}

	void Storage::run() {
		Utils::SetThreadName("strg-updtr");
		Running_ = true;
		bool FirstRun = true;
		long Retry = 2000;
		while (Running_) {
			if (!FirstRun)
				Poco::Thread::trySleep(Retry);
			if (!Running_)
				break;
			FirstRun = false;
			Retry = 2000;
		}
	}

	void Storage::Stop() {
		poco_notice(Logger(), "Stopping...");
		Running_ = false;
		Timer_.stop();
		Updater_.wakeUp();
		Updater_.join();
		poco_notice(Logger(), "Stopped...");
	}
} // namespace OpenWifi

// namespace
