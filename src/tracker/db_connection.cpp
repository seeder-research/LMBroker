#include "tracker/db_connection.h"
#include <spdlog/spdlog.h>

namespace tracker {

DbConnection::DbConnection(const std::string& connstr) : connstr_(connstr) {
    conn_ = PQconnectdb(connstr_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK)
        spdlog::error("[db] Connect failed: {}", PQerrorMessage(conn_));
    else
        spdlog::info("[db] Connected to PostgreSQL");
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

// ── internal helpers ──────────────────────────────────────────────────────────

bool DbConnection::check_ok(PGresult* res, const std::string& ctx) {
    if (!res) {
        spdlog::error("[db] {} — null result", ctx);
        return false;
    }
    ExecStatusType s = PQresultStatus(res);
    if (s == PGRES_COMMAND_OK || s == PGRES_TUPLES_OK) return true;
    spdlog::error("[db] {} — {}", ctx, PQerrorMessage(conn_));
    return false;
}

DbResult DbConnection::pg_result_to_rows(PGresult* res) {
    DbResult rows;
    int ncols = PQnfields(res);
    int nrows = PQntuples(res);
    rows.reserve(nrows);
    for (int r = 0; r < nrows; ++r) {
        DbRow row;
        row.reserve(ncols);
        for (int c = 0; c < ncols; ++c) {
            std::string col_name = PQfname(res, c);
            std::string val = PQgetisnull(res, r, c)
                              ? "" : PQgetvalue(res, r, c);
            row.emplace_back(col_name, val);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// ── public API ────────────────────────────────────────────────────────────────

bool DbConnection::execute(const std::string& sql) {
    if (!is_connected()) reconnect();
    PGresult* res = PQexec(conn_, sql.c_str());
    bool ok = check_ok(res, "execute");
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
    bool ok = check_ok(res, "execute_params");
    PQclear(res);
    return ok;
}

DbResult DbConnection::query(const std::string& sql) {
    if (!is_connected()) reconnect();
    PGresult* res = PQexec(conn_, sql.c_str());
    if (!check_ok(res, "query")) { PQclear(res); return {}; }
    auto rows = pg_result_to_rows(res);
    PQclear(res);
    return rows;
}

DbResult DbConnection::query_params(const std::string& sql,
                                     int nparams,
                                     const char* const* values) {
    if (!is_connected()) reconnect();
    PGresult* res = PQexecParams(conn_, sql.c_str(),
                                  nparams, nullptr, values,
                                  nullptr, nullptr, 0);
    if (!check_ok(res, "query_params")) { PQclear(res); return {}; }
    auto rows = pg_result_to_rows(res);
    PQclear(res);
    return rows;
}

std::string DbConnection::query_scalar(const std::string& sql,
                                        const std::string& default_val) {
    auto rows = query(sql);
    if (rows.empty() || rows[0].empty()) return default_val;
    return rows[0][0].second;
}

bool DbConnection::begin()    { return execute("BEGIN"); }
bool DbConnection::commit()   { return execute("COMMIT"); }
bool DbConnection::rollback() { return execute("ROLLBACK"); }

} // namespace tracker
