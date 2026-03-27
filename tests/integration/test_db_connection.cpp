#include <gtest/gtest.h>
#include <cstdlib>
#include "tracker/db_connection.h"

// Requires environment variable TEST_DB_CONNSTR to be set.
// Example: export TEST_DB_CONNSTR="host=localhost dbname=flexlm user=broker password=secret"

class DbConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* cs = std::getenv("TEST_DB_CONNSTR");
        if (!cs) GTEST_SKIP() << "TEST_DB_CONNSTR not set — skipping integration tests";
        connstr_ = cs;
    }
    std::string connstr_;
};

TEST_F(DbConnectionTest, ConnectsSuccessfully) {
    tracker::DbConnection db(connstr_);
    EXPECT_TRUE(db.is_connected());
}

TEST_F(DbConnectionTest, ExecuteSimpleQuery) {
    tracker::DbConnection db(connstr_);
    ASSERT_TRUE(db.is_connected());
    EXPECT_TRUE(db.execute("SELECT 1"));
}

TEST_F(DbConnectionTest, SchemaTablesExist) {
    tracker::DbConnection db(connstr_);
    ASSERT_TRUE(db.is_connected());
    // Each table should be queryable
    for (const auto& tbl : {"servers", "features", "checkouts", "denials", "health_events"}) {
        std::string sql = std::string("SELECT 1 FROM ") + tbl + " LIMIT 0";
        EXPECT_TRUE(db.execute(sql)) << "Table missing: " << tbl;
    }
}

TEST_F(DbConnectionTest, InsertAndQueryServer) {
    tracker::DbConnection db(connstr_);
    ASSERT_TRUE(db.is_connected());

    // Clean up first
    db.execute("DELETE FROM servers WHERE host = 'test-integration-host'");

    const char* vals[] = {"test-integration-host", "27099", "integration-test"};
    EXPECT_TRUE(db.execute_params(
        "INSERT INTO servers (host, port, name) VALUES ($1, $2, $3)",
        3, vals));

    EXPECT_TRUE(db.execute(
        "SELECT id FROM servers WHERE host = 'test-integration-host'"));

    // Cleanup
    db.execute("DELETE FROM servers WHERE host = 'test-integration-host'");
}
