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
					"Apply the required database schema before starting the service.";
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

	bool Storage::ValidatePostgreSQLQuery(const std::string &statement,
										  const std::string &failureMessage) {
		try {
			Poco::Data::Session Session = Pool_->get();
			uint64_t Count = 0;

			Session << statement, Poco::Data::Keywords::into(Count), Poco::Data::Keywords::now;
			if (Count == 0)
				return true;

			poco_fatal(Logger(), fmt::format("{}: {} mismatch(es)", failureMessage, Count));
		} catch (const Poco::Exception &E) {
			Logger().log(E);
		}
		return false;
	}

	bool Storage::ValidatePostgreSQLSchema() {
		static const std::string TableValidation = R"SQL(
			with expected(table_name) as (
				values
					('boards'),
					('timepoints'),
					('wificlienthistory')
			),
			actual(table_name) as (
				select table_name
				from information_schema.tables
				where table_schema = current_schema()
				  and table_type = 'BASE TABLE'
				  and table_name in ('boards', 'timepoints', 'wificlienthistory')
			)
			select count(*)
			from (
				select table_name from expected
				except
				select table_name from actual
				union all
				select table_name from actual
				except
				select table_name from expected
			) mismatches
		)SQL";

		static const std::string ColumnValidation = R"SQL(
			with expected(table_name, column_name, data_type, character_maximum_length, is_nullable) as (
				values
					('boards', 'id', 'character varying', 64, 'NO'),
					('boards', 'name', 'text', null::integer, 'YES'),
					('boards', 'description', 'text', null::integer, 'YES'),
					('boards', 'notes', 'text', null::integer, 'YES'),
					('boards', 'created', 'bigint', null::integer, 'YES'),
					('boards', 'modified', 'bigint', null::integer, 'YES'),
					('boards', 'venueid', 'text', null::integer, 'YES'),
					('boards', 'venuename', 'text', null::integer, 'YES'),
					('boards', 'venuedescription', 'text', null::integer, 'YES'),
					('boards', 'retention', 'bigint', null::integer, 'YES'),
					('boards', 'interval', 'bigint', null::integer, 'YES'),
					('boards', 'monitorsubvenues', 'boolean', null::integer, 'YES'),
					('timepoints', 'id', 'character varying', 64, 'NO'),
					('timepoints', 'boardid', 'text', null::integer, 'YES'),
					('timepoints', 'timestamp', 'bigint', null::integer, 'YES'),
					('timepoints', 'ap_data', 'text', null::integer, 'YES'),
					('timepoints', 'ssid_data', 'text', null::integer, 'YES'),
					('timepoints', 'radio_data', 'text', null::integer, 'YES'),
					('timepoints', 'device_info', 'text', null::integer, 'YES'),
					('timepoints', 'serialnumber', 'text', null::integer, 'YES'),
					('wificlienthistory', 'timestamp', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'station_id', 'text', null::integer, 'YES'),
					('wificlienthistory', 'bssid', 'text', null::integer, 'YES'),
					('wificlienthistory', 'ssid', 'text', null::integer, 'YES'),
					('wificlienthistory', 'rssi', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_bitrate', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_chwidth', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_mcs', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_nss', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_vht', 'boolean', null::integer, 'YES'),
					('wificlienthistory', 'tx_bitrate', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_chwidth', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_mcs', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_nss', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_vht', 'boolean', null::integer, 'YES'),
					('wificlienthistory', 'rx_bytes', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_bytes', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_duration', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_duration', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'rx_packets', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_packets', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'ipv4', 'text', null::integer, 'YES'),
					('wificlienthistory', 'ipv6', 'text', null::integer, 'YES'),
					('wificlienthistory', 'channel_width', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'noise', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_power', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'channel', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'active_ms', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'busy_ms', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'receive_ms', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'mode', 'text', null::integer, 'YES'),
					('wificlienthistory', 'ack_signal', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'ack_signal_avg', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'connected', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'inactive', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'tx_retries', 'bigint', null::integer, 'YES'),
					('wificlienthistory', 'venue_id', 'text', null::integer, 'YES')
			),
			actual as (
				select table_name, column_name, data_type, character_maximum_length, is_nullable
				from information_schema.columns
				where table_schema = current_schema()
				  and table_name in ('boards', 'timepoints', 'wificlienthistory')
			)
			select count(*)
			from (
				select table_name, column_name, data_type, character_maximum_length, is_nullable
				from expected
				except
				select table_name, column_name, data_type, character_maximum_length, is_nullable
				from actual
				union all
				select table_name, column_name, data_type, character_maximum_length, is_nullable
				from actual
				except
				select table_name, column_name, data_type, character_maximum_length, is_nullable
				from expected
			) mismatches
		)SQL";

		static const std::string PrimaryKeyValidation = R"SQL(
			with expected(table_name, columns) as (
				values
					('boards', 'id'),
					('timepoints', 'id')
			),
			actual as (
				select cls.relname as table_name,
					   string_agg(att.attname, ',' order by keys.ordinality) as columns
				from pg_constraint con
				join pg_class cls on cls.oid = con.conrelid
				join pg_namespace ns on ns.oid = cls.relnamespace
				join lateral unnest(con.conkey) with ordinality as keys(attnum, ordinality) on true
				join pg_attribute att on att.attrelid = cls.oid and att.attnum = keys.attnum
				where ns.nspname = current_schema()
				  and con.contype = 'p'
				  and cls.relname in ('boards', 'timepoints', 'wificlienthistory')
				group by cls.relname
			)
			select count(*)
			from (
				select table_name, columns from expected
				except
				select table_name, columns from actual
				union all
				select table_name, columns from actual
				except
				select table_name, columns from expected
			) mismatches
		)SQL";

		static const std::string IndexValidation = R"SQL(
			with expected(index_name, table_name, is_unique, columns) as (
				values
					('boards_name_index', 'boards', false, 'name:ASC'),
					('timepoint_board_index', 'timepoints', false, 'boardid:ASC,timestamp:ASC'),
					('timepoint_serial_time_index', 'timepoints', false, 'serialnumber:ASC,timestamp:ASC'),
					('stationid_name_index', 'wificlienthistory', false, 'station_id:ASC'),
					('station_ven_ts_id_name_index', 'wificlienthistory', false, 'venue_id:ASC,station_id:ASC,timestamp:ASC')
			),
			actual as (
				select idx_cls.relname as index_name,
					   tbl_cls.relname as table_name,
					   idx.indisunique as is_unique,
					   string_agg(
						   att.attname || ':' ||
						   case when (coalesce(opts.option, 0) & 1) = 1 then 'DESC' else 'ASC' end,
						   ',' order by keys.ordinality
					   ) as columns
				from pg_index idx
				join pg_class idx_cls on idx_cls.oid = idx.indexrelid
				join pg_class tbl_cls on tbl_cls.oid = idx.indrelid
				join pg_namespace ns on ns.oid = tbl_cls.relnamespace
				join lateral unnest(idx.indkey) with ordinality as keys(attnum, ordinality) on true
				left join lateral unnest(idx.indoption) with ordinality as opts(option, ordinality)
					on opts.ordinality = keys.ordinality
				join pg_attribute att on att.attrelid = tbl_cls.oid and att.attnum = keys.attnum
				where ns.nspname = current_schema()
				  and idx_cls.relname in (
					  'boards_name_index',
					  'timepoint_board_index',
					  'timepoint_serial_time_index',
					  'stationid_name_index',
					  'station_ven_ts_id_name_index'
				  )
				group by idx_cls.relname, tbl_cls.relname, idx.indisunique
			)
			select count(*)
			from (
				select index_name, table_name, is_unique, columns from expected
				except
				select index_name, table_name, is_unique, columns from actual
				union all
				select index_name, table_name, is_unique, columns from actual
				except
				select index_name, table_name, is_unique, columns from expected
			) mismatches
		)SQL";

		return ValidatePostgreSQLQuery(TableValidation, "PostgreSQL table validation failed") &&
			   ValidatePostgreSQLQuery(ColumnValidation, "PostgreSQL column validation failed") &&
			   ValidatePostgreSQLQuery(PrimaryKeyValidation,
									   "PostgreSQL primary-key validation failed") &&
			   ValidatePostgreSQLQuery(IndexValidation, "PostgreSQL index validation failed");
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
