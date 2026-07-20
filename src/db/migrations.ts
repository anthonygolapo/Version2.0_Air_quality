import type { Pool } from "pg";

export async function runMigrations(pool: Pool): Promise<void> {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS devices (
      device_id TEXT PRIMARY KEY,
      status TEXT NOT NULL CHECK (status IN ('active', 'disabled')),
      credential_version INTEGER NOT NULL,
      secret_ciphertext TEXT NOT NULL,
      previous_credential_version INTEGER,
      previous_secret_ciphertext TEXT,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE TABLE IF NOT EXISTS readings_queue (
      id BIGSERIAL PRIMARY KEY,
      device_id TEXT NOT NULL,
      sequence_number INTEGER NOT NULL,
      measured_at TIMESTAMPTZ NOT NULL,
      received_at TIMESTAMPTZ NOT NULL,
      payload_hash TEXT NOT NULL,
      payload_json JSONB NOT NULL,
      pm1 DOUBLE PRECISION NOT NULL,
      pm25 DOUBLE PRECISION NOT NULL,
      pm10 DOUBLE PRECISION NOT NULL,
      co DOUBLE PRECISION NOT NULL,
      no2 DOUBLE PRECISION NOT NULL,
      o3 DOUBLE PRECISION NOT NULL,
      so2 DOUBLE PRECISION NOT NULL,
      temperature_c DOUBLE PRECISION NOT NULL,
      humidity_percent DOUBLE PRECISION NOT NULL,
      battery_voltage DOUBLE PRECISION NOT NULL,
      solar_voltage DOUBLE PRECISION NOT NULL,
      signal_strength INTEGER NOT NULL,
      firmware_version TEXT NOT NULL,
      alarm_flags INTEGER NOT NULL,
      status TEXT NOT NULL CHECK (status IN ('pending', 'processing', 'sent', 'failed')),
      retry_count INTEGER NOT NULL DEFAULT 0,
      next_retry_at TIMESTAMPTZ,
      last_error TEXT,
      claimed_by TEXT,
      claimed_at TIMESTAMPTZ,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      sent_at TIMESTAMPTZ,
      UNIQUE (device_id, sequence_number)
    );

    CREATE INDEX IF NOT EXISTS idx_readings_queue_ready
      ON readings_queue (status, next_retry_at, id);

    CREATE INDEX IF NOT EXISTS idx_readings_queue_claimed
      ON readings_queue (status, claimed_at);

    CREATE TABLE IF NOT EXISTS queue_dead_letters (
      id BIGSERIAL PRIMARY KEY,
      queue_id BIGINT NOT NULL,
      device_id TEXT NOT NULL,
      sequence_number INTEGER NOT NULL,
      reason TEXT NOT NULL,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE TABLE IF NOT EXISTS rate_limit_buckets (
      scope TEXT NOT NULL,
      key TEXT NOT NULL,
      window_start TIMESTAMPTZ NOT NULL,
      hit_count INTEGER NOT NULL,
      PRIMARY KEY (scope, key)
    );
  `);
}
