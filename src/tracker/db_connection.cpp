#include "tracker/db_connection.h"
#include <spdlog/spdlog.h>

namespace tracker {

DbConnection::DbConnection(const std::string& connstr) : connstr_(connstr) {
    conn_ = PQconnectdb(connstr_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        spdlog::error("[db] Connect failed: {}", PQerrorMessage(conn_));
    } else {
        spdlog::info("[db] Connected to PostgreSQL");
    }
}

DbConnection::~DbConnection() {
    if (conn_) { PQfinish(conn_); conn_ = nullptr; }
}

bool DbConnection::is_connected() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

void DbConnection::reconnect() {
    PQreset(conn_);
    if (PQstatus(conn_) != CONNECTION_OK)
        spdlog::error("[db] Reconnect failed: {}", PQerrorMessage(conn_));
    else
        spdlog::info("[db] Reconnected to PostgreSQL");
}

bool DbConnection::execute(const std::string& sql) {
    if (!is_connected()) reconnect();
    PGresult* res = PQexec(conn_, sql.c_str());
    ExecStatusType status = PQresultStatus(res);
    bool ok = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
    if (!ok)
        spdlog::error("[db] execute error: {}", PQerrorMessage(conn_));
    PQclear(res);
    return ok;
}

bool DbConnection::execute_params(const std::string& sql,
                                   int nparams,
                                   const char* const* values) {
    if (!is_connected()) reconnect();
    PGresult* res = PQexecParams(conn_, sql.c_str(),
                                  nparams, nullptr, values,
                                  nullptr, nullptr, 0);
    ExecStatusType status = PQresultStatus(res);
    bool ok = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
    if (!ok)
        spdlog::error("[db] execute_params error: {}", PQerrorMessage(conn_));
    PQclear(res);
    return ok;
}

} // namespace tracker
