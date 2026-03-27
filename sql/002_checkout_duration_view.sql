-- Migration 002: add checkout duration view and peak utilisation view

DO $$ BEGIN
  IF EXISTS (SELECT 1 FROM schema_migrations WHERE version = 2) THEN
    RAISE NOTICE 'Migration 002 already applied, skipping.';
    RETURN;
  END IF;

  -- Active checkouts with running duration
  CREATE OR REPLACE VIEW v_active_checkouts AS
  SELECT
      id,
      feature,
      username,
      client_host,
      backend_host,
      backend_port,
      checked_out_at,
      EXTRACT(EPOCH FROM (NOW() - checked_out_at))::INTEGER AS duration_sec
  FROM checkouts
  WHERE checked_in_at IS NULL
  ORDER BY checked_out_at;

  -- Denial summary by feature + hour (useful for Grafana)
  CREATE OR REPLACE VIEW v_denial_hourly AS
  SELECT
      date_trunc('hour', denied_at) AS hour,
      feature,
      COUNT(*)                       AS denial_count
  FROM denials
  GROUP BY 1, 2
  ORDER BY 1 DESC, 3 DESC;

  INSERT INTO schema_migrations (version, description)
  VALUES (2, 'checkout duration and denial hourly views');

END $$;
