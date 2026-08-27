#include <iostream>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include "Poco/Data/Session.h"
#include "Poco/Data/SessionPool.h"
#include "Poco/Data/PostgreSQL/Connector.h"
#include "Poco/Data/SQLite/Connector.h"
#include "Poco/Logger.h"
#include "storage/storage_boards.h"
#include "framework/utils.h"

using namespace OpenWifi;

namespace OpenWifi {
	void DaemonPostInitialization(Poco::Util::Application &) {}
}

static Poco::Logger &GetTestLogger() {
	return Poco::Logger::get("TestLogger");
}

static bool CheckColumnExists(Poco::Data::Session &Session, const std::string &TableName, const std::string &ColumnName) {
	try {
		Poco::Data::Statement Stmt(Session);
		Stmt << fmt::format("SELECT {} FROM {} LIMIT 1", ColumnName, TableName), Poco::Data::Keywords::now;
		return true;
	} catch (...) {
		return false;
	}
}

static std::string GetTempDbPath(const std::string &TestName) {
	std::filesystem::create_directories("test_dbs");
	std::string Path = "test_dbs/" + TestName + ".db";
	if (std::filesystem::exists(Path)) {
		std::filesystem::remove(Path);
	}
	return Path;
}

static void CleanDbFile(const std::string &Path) {
	if (std::filesystem::exists(Path)) {
		std::filesystem::remove(Path);
	}
}

// 1. Successful migration from one venueList entry.
void test_1_successful_migration_single_venuelist() {
	std::cout << "Running Test 1: Successful migration from single venueList entry..." << std::endl;
	std::string DbPath = GetTempDbPath("test_1");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b1', 'Board 1', 'Desc 1', '[]', 1000, 2000, '[{\"id\":\"v1\",\"name\":\"Venue 1\",\"description\":\"VDesc 1\",\"retention\":3600,\"interval\":60,\"monitorSubVenues\":true}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == true);

	{
		Poco::Data::Session Session = Pool.get();
		assert(!CheckColumnExists(Session, "boards", "venuelist"));
		assert(!CheckColumnExists(Session, "boards", "venue"));
	}

	AnalyticsObjects::BoardInfo Board;
	assert(Db.GetRecord("id", "b1", Board) == true);
	assert(Board.venue.id == "v1");
	assert(Board.venue.name == "Venue 1");
	assert(Board.venue.description == "VDesc 1");
	assert(Board.venue.retention == 3600);
	assert(Board.venue.interval == 60);
	assert(Board.venue.monitorSubVenues == true);
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 2. A list containing multiple venues uses only the first entry.
void test_2_multi_venue_list_uses_first_entry() {
	std::cout << "Running Test 2: Multi-venue list uses first entry..." << std::endl;
	std::string DbPath = GetTempDbPath("test_2");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b2', 'Board 2', 'Desc 2', '[]', 1000, 2000, '[{\"id\":\"v1\",\"name\":\"First Venue\"}, {\"id\":\"v2\",\"name\":\"Second Venue\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == true);

	AnalyticsObjects::BoardInfo Board;
	assert(Db.GetRecord("id", "b2", Board) == true);
	assert(Board.venue.id == "v1");
	assert(Board.venue.name == "First Venue");
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 3. Existing populated typed columns are not overwritten.
void test_3_existing_populated_typed_columns_preserved() {
	std::cout << "Running Test 3: Existing populated typed columns are preserved..." << std::endl;
	std::string DbPath = GetTempDbPath("test_3");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueid TEXT, venuename TEXT, venuedescription TEXT, retention BIGINT, interval BIGINT, monitorsubvenues BOOLEAN, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b3', 'Board 3', '', '[]', 0, 0, 'v_existing', 'Existing Venue', 'Desc Existing', 100, 10, 0, '[{\"id\":\"v_new\",\"name\":\"New Venue\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == true);

	AnalyticsObjects::BoardInfo Board;
	assert(Db.GetRecord("id", "b3", Board) == true);
	assert(Board.venue.id == "v_existing");
	assert(Board.venue.name == "Existing Venue");
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 4. The venue column contains valid data but is ignored.
void test_4_venue_column_ignored() {
	std::cout << "Running Test 4: Obsolete venue column is ignored..." << std::endl;
	std::string DbPath = GetTempDbPath("test_4");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venue TEXT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b4', 'Board 4', '', '[]', 0, 0, '{\"id\":\"v_ignored\",\"name\":\"Ignored Venue\"}', '[{\"id\":\"v_list\",\"name\":\"List Venue\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == true);

	AnalyticsObjects::BoardInfo Board;
	assert(Db.GetRecord("id", "b4", Board) == true);
	assert(Board.venue.id == "v_list");
	assert(Board.venue.name == "List Venue");

	{
		Poco::Data::Session Session = Pool.get();
		assert(!CheckColumnExists(Session, "boards", "venue"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 5. venueList is empty.
void test_5_empty_venuelist_fails_migration() {
	std::cout << "Running Test 5: Empty venueList fails migration..." << std::endl;
	std::string DbPath = GetTempDbPath("test_5");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b5', 'Board 5', '', '[]', 0, 0, '[]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == false);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "venuelist"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 6. venueList contains malformed JSON.
void test_6_malformed_json_fails_migration() {
	std::cout << "Running Test 6: Malformed JSON fails migration..." << std::endl;
	std::string DbPath = GetTempDbPath("test_6");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b6', 'Board 6', '', '[]', 0, 0, 'invalid json');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == false);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "venuelist"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 7. The first venue has an empty ID.
void test_7_first_venue_empty_id_fails_migration() {
	std::cout << "Running Test 7: First venue with empty ID fails migration..." << std::endl;
	std::string DbPath = GetTempDbPath("test_7");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b7', 'Board 7', '', '[]', 0, 0, '[{\"id\":\"\",\"name\":\"No ID\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == false);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "venuelist"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 8. One invalid board rolls back every migrated board.
void test_8_invalid_board_rolls_back_all() {
	std::cout << "Running Test 8: One invalid board rolls back every board..." << std::endl;
	std::string DbPath = GetTempDbPath("test_8");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b8_1', 'Board 8.1', '', '[]', 0, 0, '[{\"id\":\"v1\",\"name\":\"Valid\"}]');", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b8_2', 'Board 8.2', '', '[]', 0, 0, '[]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == false);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "venuelist"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 9. DDL failure rolls back the transaction.
void test_9_ddl_failure_rolls_back() {
	std::cout << "Running Test 9: DDL failure rolls back transaction..." << std::endl;
	std::string DbPath = GetTempDbPath("test_9");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		// Create boards as a VIEW so ALTER TABLE ADD COLUMN fails with DDL error
		PoolSession << "CREATE TABLE boards_base (id TEXT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "CREATE VIEW boards AS SELECT id, venueList FROM boards_base;", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards_base VALUES ('b9', '[{\"id\":\"v1\",\"name\":\"Valid\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	// Upgrade tries to add missing columns to VIEW, which causes DDL failure
	bool Result = Db.Create();
	assert(Result == false);

	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 10. Validation failure preserves both legacy columns.
void test_10_validation_failure_preserves_legacy_columns() {
	std::cout << "Running Test 10: Validation failure preserves legacy columns..." << std::endl;
	std::string DbPath = GetTempDbPath("test_10");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venue TEXT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b10', 'Board 10', '', '[]', 0, 0, '{\"id\":\"v1\"}', '[{\"id\":\"\",\"name\":\"Invalid\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == false);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "venuelist"));
		assert(CheckColumnExists(Session, "boards", "venue"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 11 & 12. Successful migration removes venuelist and venue.
void test_11_12_migration_removes_legacy_columns() {
	std::cout << "Running Tests 11 & 12: Successful migration removes venuelist and venue..." << std::endl;
	std::string DbPath = GetTempDbPath("test_11_12");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venue TEXT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b11', 'Board 11', '', '[]', 0, 0, '{\"id\":\"old\"}', '[{\"id\":\"v11\",\"name\":\"V11\"}]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == true);

	{
		Poco::Data::Session Session = Pool.get();
		assert(!CheckColumnExists(Session, "boards", "venuelist"));
		assert(!CheckColumnExists(Session, "boards", "venue"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 13. A migration retry succeeds after a previous rollback.
void test_13_migration_retry_succeeds() {
	std::cout << "Running Test 13: Migration retry succeeds after previous rollback..." << std::endl;
	std::string DbPath = GetTempDbPath("test_13");
	{
		Poco::Data::Session PoolSession("SQLite", DbPath);
		PoolSession << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, notes TEXT, created BIGINT, modified BIGINT, venueList TEXT);", Poco::Data::Keywords::now;
		PoolSession << "INSERT INTO boards VALUES ('b13', 'Board 13', '', '[]', 0, 0, '[]');", Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());

	// First attempt: fails because venueList is empty
	bool Result1 = Db.Create();
	assert(Result1 == false);

	// Fix row in DB
	{
		Poco::Data::Session FixSession = Pool.get();
		FixSession << "UPDATE boards SET venueList='[{\"id\":\"v13\",\"name\":\"Fixed Venue\"}]' WHERE id='b13';", Poco::Data::Keywords::now;
	}

	// Retry attempt: succeeds!
	bool Result2 = Db.Create();
	assert(Result2 == true);

	AnalyticsObjects::BoardInfo Board;
	assert(Db.GetRecord("id", "b13", Board) == true);
	assert(Board.venue.id == "v13");
	assert(Board.venue.name == "Fixed Venue");

	{
		Poco::Data::Session Session = Pool.get();
		assert(!CheckColumnExists(Session, "boards", "venuelist"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 14. Fresh database creation contains only the final typed columns.
void test_14_fresh_db_creation() {
	std::cout << "Running Test 14: Fresh database creation contains only final typed columns..." << std::endl;
	std::string DbPath = GetTempDbPath("test_14");

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	bool Result = Db.Create();
	assert(Result == true);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "id"));
		assert(CheckColumnExists(Session, "boards", "name"));
		assert(CheckColumnExists(Session, "boards", "description"));
		assert(CheckColumnExists(Session, "boards", "notes"));
		assert(CheckColumnExists(Session, "boards", "created"));
		assert(CheckColumnExists(Session, "boards", "modified"));
		assert(CheckColumnExists(Session, "boards", "venueid"));
		assert(CheckColumnExists(Session, "boards", "venuename"));
		assert(CheckColumnExists(Session, "boards", "venuedescription"));
		assert(CheckColumnExists(Session, "boards", "retention"));
		assert(CheckColumnExists(Session, "boards", "interval"));
		assert(CheckColumnExists(Session, "boards", "monitorsubvenues"));
		assert(!CheckColumnExists(Session, "boards", "venuelist"));
		assert(!CheckColumnExists(Session, "boards", "venue"));
	}
	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

// 15. Board create, get, update and list operations work after migration.
void test_15_board_crud_after_migration() {
	std::cout << "Running Test 15: Board CRUD operations work after migration..." << std::endl;
	std::string DbPath = GetTempDbPath("test_15");

	Poco::Data::SessionPool Pool("SQLite", DbPath);
	BoardsDB Db(DBType::sqlite, Pool, GetTestLogger());
	assert(Db.Create() == true);

	// Create
	AnalyticsObjects::BoardInfo NewBoard;
	NewBoard.info.id = "b15";
	NewBoard.info.name = "Board 15";
	NewBoard.info.description = "Test Board 15";
	NewBoard.info.created = 12345;
	NewBoard.info.modified = 12345;
	NewBoard.venue.id = "v15";
	NewBoard.venue.name = "Venue 15";
	NewBoard.venue.description = "V15 Description";
	NewBoard.venue.retention = 86400;
	NewBoard.venue.interval = 300;
	NewBoard.venue.monitorSubVenues = true;

	assert(Db.CreateRecord(NewBoard) == true);

	// Get
	AnalyticsObjects::BoardInfo FetchedBoard;
	assert(Db.GetRecord("id", "b15", FetchedBoard) == true);
	assert(FetchedBoard.info.name == "Board 15");
	assert(FetchedBoard.venue.id == "v15");
	assert(FetchedBoard.venue.name == "Venue 15");

	// Update
	FetchedBoard.info.name = "Updated Board 15";
	FetchedBoard.venue.name = "Updated Venue 15";
	assert(Db.UpdateRecord("id", "b15", FetchedBoard) == true);

	AnalyticsObjects::BoardInfo UpdatedBoard;
	assert(Db.GetRecord("id", "b15", UpdatedBoard) == true);
	assert(UpdatedBoard.info.name == "Updated Board 15");
	assert(UpdatedBoard.venue.name == "Updated Venue 15");

	// List
	BoardsDB::RecordVec List;
	assert(Db.GetRecords(0, 10, List) == true);
	assert(List.size() == 1);
	assert(List[0].info.id == "b15");

	CleanDbFile(DbPath);
	std::cout << "  Passed!" << std::endl;
}

void run_sqlite_tests() {
	std::cout << "==========================================" << std::endl;
	std::cout << "Running Board Database Migration Tests..." << std::endl;
	std::cout << "==========================================" << std::endl;

	test_1_successful_migration_single_venuelist();
	test_2_multi_venue_list_uses_first_entry();
	test_3_existing_populated_typed_columns_preserved();
	test_4_venue_column_ignored();
	test_5_empty_venuelist_fails_migration();
	test_6_malformed_json_fails_migration();
	test_7_first_venue_empty_id_fails_migration();
	test_8_invalid_board_rolls_back_all();
	test_9_ddl_failure_rolls_back();
	test_10_validation_failure_preserves_legacy_columns();
	test_11_12_migration_removes_legacy_columns();
	test_13_migration_retry_succeeds();
	test_14_fresh_db_creation();
	test_15_board_crud_after_migration();

	std::cout << "==========================================" << std::endl;
	std::cout << "ALL 15 TESTS PASSED SUCCESSFULLY!" << std::endl;
	std::cout << "==========================================" << std::endl;
}

int run_postgresql_test() {
	const char *ConnectionString = std::getenv("BOARD_MIGRATION_TEST_POSTGRESQL_CONN");
	if (ConnectionString == nullptr || std::string(ConnectionString).empty()) {
		std::cout << "Skipping PostgreSQL board migration test: "
				  << "BOARD_MIGRATION_TEST_POSTGRESQL_CONN is not set." << std::endl;
		return 77;
	}

	Poco::Data::PostgreSQL::Connector::registerConnector();
	std::cout << "Running PostgreSQL board migration test..." << std::endl;

	{
		Poco::Data::Session Session("PostgreSQL", ConnectionString);
		Session << "DROP TABLE IF EXISTS boards", Poco::Data::Keywords::now;
		Session << "CREATE TABLE boards (id TEXT PRIMARY KEY, name TEXT, description TEXT, "
				   "notes TEXT, created BIGINT, modified BIGINT, venueList TEXT)",
			Poco::Data::Keywords::now;
		Session << "INSERT INTO boards VALUES ('pg_b1', 'PG Board 1', 'Desc', '[]', 1000, "
				   "2000, '[{\"id\":\"pg_v1\",\"name\":\"PG Venue\",\"description\":\"PG "
				   "Desc\",\"retention\":3600,\"interval\":60,\"monitorSubVenues\":true}]')",
			Poco::Data::Keywords::now;
	}

	Poco::Data::SessionPool Pool("PostgreSQL", ConnectionString);
	BoardsDB Db(DBType::pgsql, Pool, GetTestLogger());
	assert(Db.Create() == true);

	AnalyticsObjects::BoardInfo Board;
	assert(Db.GetRecord("id", "pg_b1", Board) == true);
	assert(Board.venue.id == "pg_v1");
	assert(Board.venue.name == "PG Venue");
	assert(Board.venue.description == "PG Desc");
	assert(Board.venue.retention == 3600);
	assert(Board.venue.interval == 60);
	assert(Board.venue.monitorSubVenues == true);

	{
		Poco::Data::Session Session = Pool.get();
		assert(CheckColumnExists(Session, "boards", "venueid"));
		assert(CheckColumnExists(Session, "boards", "venuename"));
		assert(CheckColumnExists(Session, "boards", "venuedescription"));
		assert(CheckColumnExists(Session, "boards", "retention"));
		assert(CheckColumnExists(Session, "boards", "interval"));
		assert(CheckColumnExists(Session, "boards", "monitorsubvenues"));
		assert(!CheckColumnExists(Session, "boards", "venuelist"));
		assert(!CheckColumnExists(Session, "boards", "venue"));
		Session << "DROP TABLE boards", Poco::Data::Keywords::now;
	}

	std::cout << "PostgreSQL board migration test passed." << std::endl;
	return 0;
}

int main(int argc, char **argv) {
	if (argc > 1 && std::string(argv[1]) == "--postgresql") {
		return run_postgresql_test();
	}

	Poco::Data::SQLite::Connector::registerConnector();
	run_sqlite_tests();
	return 0;
}
