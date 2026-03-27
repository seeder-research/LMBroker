#pragma once
#include <string>
#include <vector>
#include <libpq-fe.h>

namespace tracker {

class DbConnection {
public:
    explicit DbConnection(const std::string& connstr);
    ~DbConnection();

    bool is_connected() const;

    // Execute a statement with no results (INSERT/UPDATE/DELETE/DDL)
    bool execute(const std::string& sql);

    // Parameterized query — values are null-terminated C strings
    bool execute_params(const std::string& sql,
                        int nparams,
                        const char* const* values);

    // Reconnect after lost connection
    void reconnect();

private:
    PGconn*     conn_{nullptr};
    std::string connstr_;
};

} // namespace tracker
