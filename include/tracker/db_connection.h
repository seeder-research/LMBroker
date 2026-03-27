#pragma once
#include <string>
#include <vector>
#include <libpq-fe.h>

namespace tracker {

// Simple row — maps column name → string value
using DbRow    = std::vector<std::pair<std::string, std::string>>;
using DbResult = std::vector<DbRow>;

class DbConnection {
public:
    explicit DbConnection(const std::string& connstr);
    ~DbConnection();

    bool is_connected() const;
    void reconnect();

    // DDL / DML with no result set
    bool execute(const std::string& sql);

    // Parameterized DML (INSERT / UPDATE / DELETE)
    bool execute_params(const std::string& sql,
                        int nparams,
                        const char* const* values);

    // SELECT — returns rows as vector<DbRow>; empty on error
    DbResult query(const std::string& sql);

    // Parameterized SELECT
    DbResult query_params(const std::string& sql,
                          int nparams,
                          const char* const* values);

    // Convenience: return single cell as string, or default_val on miss
    std::string query_scalar(const std::string& sql,
                             const std::string& default_val = "");

    // Transaction helpers
    bool begin();
    bool commit();
    bool rollback();

private:
    PGconn*     conn_{nullptr};
    std::string connstr_;

    DbResult pg_result_to_rows(PGresult* res);
    bool     check_ok(PGresult* res, const std::string& context);
};

} // namespace tracker
