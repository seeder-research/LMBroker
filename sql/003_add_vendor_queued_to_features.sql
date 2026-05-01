-- Migration 003: add vendor and queued columns to features table

DO $$ BEGIN
  IF EXISTS (SELECT 1 FROM schema_migrations WHERE version = 3) THEN
    RAISE NOTICE 'Migration 003 already applied, skipping.';
    RETURN;
  END IF;

  ALTER TABLE features
    ADD COLUMN IF NOT EXISTS vendor TEXT,
    ADD COLUMN IF NOT EXISTS queued INTEGER NOT NULL DEFAULT 0;

  -- Rebuild view to include queued
  CREATE OR REPLACE VIEW v_license_utilisation AS
  SELECT
      f.feature,
      SUM(f.total)            AS total,
      SUM(f.in_use)           AS in_use,
      SUM(f.total - f.in_use) AS available,
      SUM(f.queued)           AS queued,
      MAX(f.polled_at)        AS last_polled
  FROM features f
  WHERE f.polled_at = (
      SELECT MAX(f2.polled_at) FROM features f2
      WHERE f2.server_id = f.server_id AND f2.feature = f.feature
  )
  GROUP BY f.feature;

  INSERT INTO schema_migrations (version, description)
  VALUES (3, 'add vendor and queued columns to features');

END $$;
