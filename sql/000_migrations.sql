-- Migration tracking table — apply this first, once, on a fresh database.
-- setup_db.sh handles this automatically.
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER     NOT NULL PRIMARY KEY,
    description TEXT        NOT NULL,
    applied_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
