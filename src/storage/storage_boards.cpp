//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include "storage_boards.h"
#include "fmt/format.h"
#include "framework/OpenWifiTypes.h"
#include "framework/RESTAPI_utils.h"
#include "Poco/Exception.h"
#include "Poco/Logger.h"

namespace OpenWifi {

	struct ColumnDef {
		std::string Name;
		std::string TypeDef;
	};

	static bool ColumnExists(Poco::Data::Session &Session, const std::string &TableName, const std::string &ColumnName) {
		try {
			Poco::Data::Statement Stmt(Session);
			Stmt << fmt::format("SELECT {} FROM {} LIMIT 1", ColumnName, TableName), Poco::Data::Keywords::now;
			return true;
		} catch (...) {
			return false;
		}
	}

	static void RunMigrationStatements(Poco::Data::Session &Session, const std::vector<ColumnDef> &MissingColumns, Poco::Logger &Logger) {
		for (const auto &Col : MissingColumns) {
			std::string Statement = fmt::format("ALTER TABLE boards ADD COLUMN {} {}", Col.Name, Col.TypeDef);
			try {
				Session << Statement, Poco::Data::Keywords::now;
			} catch (const Poco::Exception &E) {
				poco_error(
					Logger,
					fmt::format("Board migration DDL statement failed '{}': {}", Statement, E.displayText()));
				throw;
			} catch (const std::exception &E) {
				poco_error(
					Logger,
					fmt::format("Board migration DDL statement failed '{}': {}", Statement, E.what()));
				throw;
			} catch (...) {
				poco_error(
					Logger,
					fmt::format("Board migration DDL statement failed '{}': unknown error", Statement));
				throw;
			}
		}
	}

	static void BackfillVenueField(Poco::Data::Session &Session, Poco::Logger &Logger,
								   BoardsDB &Db, bool HasVenueColumn, bool HasVenueListColumn,
								   uint64_t &MigratedCount, uint64_t &SkippedCount,
								   uint64_t &FailedCount) {
		typedef Poco::Tuple<std::string, std::string, std::string, std::string> LegacyRecord;
		std::vector<LegacyRecord> Records;

		std::string SelectSql;
		if (HasVenueColumn && HasVenueListColumn) {
			SelectSql = "SELECT id, venueid, venue, venueList FROM boards";
		} else if (HasVenueColumn) {
			SelectSql = "SELECT id, venueid, venue, '' FROM boards";
		} else if (HasVenueListColumn) {
			SelectSql = "SELECT id, venueid, '', venueList FROM boards";
		} else {
			SelectSql = "SELECT id, venueid, '', '' FROM boards";
		}

		Poco::Data::Statement Select(Session);
		Select << SelectSql, Poco::Data::Keywords::into(Records);
		Select.execute();

		uint64_t TotalRecords = Records.size();
		poco_information(Logger, fmt::format("Migrating {} board records", TotalRecords));

		std::string vId, vName, vDesc, boardId;
		uint64_t vRetention = 0, vInterval = 0;
		bool vMonitorSubVenues = false;

		std::string UpdateSql = Db.ConvertParams(R"(
			UPDATE boards
			SET venueid=?,
				venuename=?,
				venuedescription=?,
				retention=?,
				interval=?,
				monitorsubvenues=?
			WHERE id=?
		)");

		Poco::Data::Statement UpdateStmt(Session);
		UpdateStmt << UpdateSql,
			Poco::Data::Keywords::use(vId),
			Poco::Data::Keywords::use(vName),
			Poco::Data::Keywords::use(vDesc),
			Poco::Data::Keywords::use(vRetention),
			Poco::Data::Keywords::use(vInterval),
			Poco::Data::Keywords::use(vMonitorSubVenues),
			Poco::Data::Keywords::use(boardId);

		for (const auto &Record : Records) {
			auto Id = Record.get<0>();
			auto ExistingVenueId = Record.get<1>();
			auto LegacyVenue = Record.get<2>();
			auto LegacyVenueList = Record.get<3>();

			if (!ExistingVenueId.empty()) {
				SkippedCount++;
				continue;
			}

			AnalyticsObjects::VenueInfo Venue;
			bool Parsed = false;

			// If venue contains a JSON object, migrate that object.
			// Explicit precedence: prefer the newer venue field.
			if (!LegacyVenue.empty()) {
				try {
					Poco::JSON::Parser Parser;
					auto Object = Parser.parse(LegacyVenue).extract<Poco::JSON::Object::Ptr>();
					if (Object) {
						Venue.from_json(Object);
						if (!Venue.id.empty()) {
							Parsed = true;
						}
					}
				} catch (...) {
				}
			}

			// Otherwise, if venueList contains an array, migrate its first entry.
			if (!Parsed && !LegacyVenueList.empty()) {
				try {
					auto Venues = RESTAPI_utils::to_object_array<AnalyticsObjects::VenueInfo>(LegacyVenueList);
					if (!Venues.empty() && !Venues[0].id.empty()) {
						Venue = Venues[0];
						Parsed = true;
					}
				} catch (...) {
				}
			}

			if (!Parsed || Venue.id.empty()) {
				FailedCount++;
				poco_error(
					Logger,
					fmt::format("Board migration failed for board ID '{}': JSON is malformed or contains no venue ID", Id));
				throw Poco::ApplicationException(
					fmt::format("Board migration failed for board ID '{}': JSON is malformed or contains no venue ID", Id));
			}

			vId = Venue.id;
			vName = Venue.name;
			vDesc = Venue.description;
			vRetention = Venue.retention;
			vInterval = Venue.interval;
			vMonitorSubVenues = Venue.monitorSubVenues;
			boardId = Id;

			UpdateStmt.execute();
			MigratedCount++;
		}

		// Normalize NULL values across non-optional columns for all boards
		std::vector<std::string> NormalizationStatements{
			"UPDATE boards SET venuename='' WHERE venuename IS NULL",
			"UPDATE boards SET venuedescription='' WHERE venuedescription IS NULL",
			"UPDATE boards SET retention=0 WHERE retention IS NULL",
			"UPDATE boards SET interval=0 WHERE interval IS NULL",
			"UPDATE boards SET monitorsubvenues=false WHERE monitorsubvenues IS NULL"
		};
		for (const auto &StmtStr : NormalizationStatements) {
			Poco::Data::Statement NormalizeStmt(Session);
			NormalizeStmt << StmtStr, Poco::Data::Keywords::now;
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
		Poco::Data::Session Session = Pool_.get();

		try {
			bool HasVenueColumn = ColumnExists(Session, "boards", "venue");
			bool HasVenueListColumn = ColumnExists(Session, "boards", "venueList");

			std::vector<ColumnDef> AllColumns{
				{"venueid", "TEXT"},
				{"venuename", "TEXT"},
				{"venuedescription", "TEXT"},
				{"retention", "BIGINT"},
				{"interval", "BIGINT"},
				{"monitorsubvenues", "BOOLEAN"}
			};

			std::vector<ColumnDef> MissingColumns;
			for (const auto &Col : AllColumns) {
				if (!ColumnExists(Session, "boards", Col.Name)) {
					MissingColumns.push_back(Col);
				}
			}

			Session.begin();

			// Add columns using database-supported DDL for missing columns.
			RunMigrationStatements(Session, MissingColumns, Logger());

			// Backfill all legacy records.
			uint64_t MigratedCount = 0;
			uint64_t SkippedCount = 0;
			uint64_t FailedCount = 0;
			BackfillVenueField(Session, Logger(), *this, HasVenueColumn, HasVenueListColumn,
							   MigratedCount, SkippedCount, FailedCount);

			// Verify that every board has been migrated and all fields are normalized.
			uint64_t InvalidBoards = 0;
			Session << R"(
				SELECT COUNT(*)
				FROM boards
				WHERE venueid IS NULL OR venueid = ''
				   OR venuename IS NULL
				   OR venuedescription IS NULL
				   OR retention IS NULL
				   OR interval IS NULL
				   OR monitorsubvenues IS NULL
			)",
				Poco::Data::Keywords::into(InvalidBoards),
				Poco::Data::Keywords::now;

			if (InvalidBoards != 0) {
				throw Poco::ApplicationException(
					fmt::format(
						"Board migration failed: {} records have no venue or partial venue fields",
						InvalidBoards));
			}

			Session.commit();

			poco_information(Logger(), fmt::format("Migrated: {}", MigratedCount));
			poco_information(Logger(), fmt::format("Skipped as already migrated: {}", SkippedCount));
			poco_information(Logger(), fmt::format("Failed: {}", FailedCount));
			poco_information(Logger(), "Board migration completed successfully");

			to = 2;
			return true;
		} catch (const Poco::Exception &E) {
			if (Session.isTransaction()) {
				try {
					Session.rollback();
				} catch (...) {
				}
			}
			poco_error(
				Logger(),
				fmt::format("Board database migration failed: {}", E.displayText()));
		} catch (const std::exception &E) {
			if (Session.isTransaction()) {
				try {
					Session.rollback();
				} catch (...) {
				}
			}
			poco_error(
				Logger(),
				fmt::format("Board database migration failed: {}", E.what()));
		} catch (...) {
			if (Session.isTransaction()) {
				try {
					Session.rollback();
				} catch (...) {
				}
			}
			poco_error(Logger(), "Board database migration failed: unknown error");
		}

		// Do not advance `to`.
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
