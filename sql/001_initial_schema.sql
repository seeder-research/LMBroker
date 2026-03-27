-- Migration 001: initial schema
-- Guarded by schema_migrations; applied automatically by migrate.sh

DO $$ BEGIN
  IF EXISTS (SELECT 1 FROM schema_migrations WHERE version = 1) THEN
    RAISE NOTICE 'Migration 001 already applied, skipping.';
    RETURN;
  END IF;

  CREATE TABLE IF NOT EXISTS servers (
      id         SERIAL PRIMARY KEY,
      host       TEXT    NOT NULL,
      port       INTEGER NOT NULL DEFAULT 27000,
      name       TEXT,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      UNIQUE (host, port)
  );

  CREATE TABLE IF NOT EXISTS features (
      id        SERIAL PRIMARY KEY,
      server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
      feature   TEXT    NOT NULL,
      vendor    TEXT,
      total     INTEGER NOT NULL DEFAULT 0,
      in_use    INTEGER NOT NULL DEFAULT 0,
      queued    INTEGER NOT NULL DEFAULT 0,
      polled_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
  );
  CREATE INDEX IF NOT EXISTS idx_features_feature   ON features(feature);
  CREATE INDEX IF NOT EXISTS idx_features_polled_at ON features(polled_at);
  CREATE INDEX IF NOT EXISTS idx_features_server_feature
      ON features(server_id, feature);

  CREATE TABLE IF NOT EXISTS checkouts (
      id              BIGSERIAL PRIMARY KEY,
      feature         TEXT NOT NULL,
      username        TEXT,
      client_host     TEXT,
      backend_host    TEXT,
      backend_port    INTEGER,
      checked_out_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      checked_in_at   TIMESTAMPTZ
  );
  CREATE INDEX IF NOT EXISTS idx_checkouts_feature        ON checkouts(feature);
  CREATE INDEX IF NOT EXISTS idx_checkouts_checked_out_at ON checkouts(checked_out_at);
  CREATE INDEX IF NOT EXISTS idx_checkouts_open
      ON checkouts(feature, username) WHERE checked_in_at IS NULL;

  CREATE TABLE IF NOT EXISTS denials (
      id          BIGSERIAL PRIMARY KEY,
      feature     TEXT NOT NULL,
      username    TEXT,
      client_host TEXT,
      denied_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      reason      TEXT
  );
  CREATE INDEX IF NOT EXISTS idx_denials_feature   ON denials(feature);
  CREATE INDEX IF NOT EXISTS idx_denials_denied_at ON denials(denied_at);

  CREATE TABLE IF NOT EXISTS health_events (
      id          BIGSERIAL PRIMARY KEY,
      server_id   INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
      event       TEXT    NOT NULL CHECK (event IN ('UP', 'DOWN')),
      occurred_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
  );
  CREATE INDEX IF NOT EXISTS idx_health_events_server_id   ON health_events(server_id);
  CREATE INDEX IF NOT EXISTS idx_health_events_occurred_at ON health_events(occurred_at);

  -- Views
  CREATE OR REPLACE VIEW v_license_utilisation AS
  SELECT
      f.feature,
      SUM(f.total)             AS total,
      SUM(f.in_use)            AS in_use,
      SUM(f.total - f.in_use)  AS available,
      SUM(f.queued)            AS queued,
      MAX(f.polled_at)         AS last_polled
  FROM features f
  WHERE f.polled_at = (
      SELECT MAX(f2.polled_at) FROM features f2
      WHERE f2.server_id = f.server_id AND f2.feature = f.feature
  )
  GROUP BY f.feature;

  CREATE OR REPLACE VIEW v_denial_rate_24h AS
  SELECT feature, COUNT(*) AS denials_24h
  FROM denials
  WHERE denied_at >= NOW() - INTERVAL '24 hours'
  GROUP BY feature
  ORDER BY denials_24h DESC;

  INSERT INTO schema_migrations (version, description)
  VALUES (1, 'initial schema');

END $$;
