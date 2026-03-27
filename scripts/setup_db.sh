#!/usr/bin/env bash
# Create the PostgreSQL database, user, and schema for flexlm-broker.
# Run as a user with PostgreSQL superuser privileges (e.g. postgres).
set -euo pipefail

DB="${FLEXLM_DB:-flexlm}"
USER="${FLEXLM_USER:-broker}"
PASS="${FLEXLM_PASS:-secret}"
SCHEMA="$(cd "$(dirname "$0")/.." && pwd)/sql/001_schema.sql"

echo "[db] Creating user '$USER' and database '$DB'..."
psql -U postgres <<SQL
DO \$\$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = '$USER') THEN
    CREATE USER $USER WITH PASSWORD '$PASS';
  END IF;
END
\$\$;

SELECT 'CREATE DATABASE $DB OWNER $USER'
  WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = '$DB')
\gexec
SQL

echo "[db] Applying schema..."
psql -U "$USER" -d "$DB" -f "$SCHEMA"

echo "[db] Database '$DB' ready."
echo "[db] Connection string: host=localhost dbname=$DB user=$USER password=$PASS"
