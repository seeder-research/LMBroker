#!/usr/bin/env bash
# Apply all pending SQL migrations in order.
# Safe to re-run — each migration guards itself with schema_migrations check.
set -euo pipefail

DB="${FLEXLM_DB:-flexlm}"
USER="${FLEXLM_USER:-broker}"
PGPASSWORD="${FLEXLM_PASS:-secret}"
export PGPASSWORD

SQL_DIR="$(cd "$(dirname "$0")/../sql" && pwd)"

echo "[migrate] Ensuring schema_migrations table exists..."
psql -h localhost -U "$USER" -d "$DB" -f "$SQL_DIR/000_migrations.sql" -q

echo "[migrate] Applying migrations from $SQL_DIR ..."
for f in "$SQL_DIR"/0[0-9][1-9]_*.sql; do
    echo "[migrate]   -> $(basename "$f")"
    psql -h localhost -U "$USER" -d "$DB" -f "$f" -q
done

echo "[migrate] Applied migrations:"
psql -h localhost -U "$USER" -d "$DB" -c \
    "SELECT version, description, applied_at FROM schema_migrations ORDER BY version;"
