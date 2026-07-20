import dotenv from "dotenv";

dotenv.config();

function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

export const config = {
  nodeEnv: process.env.NODE_ENV ?? "development",
  host: process.env.HOST ?? "0.0.0.0",
  port: Number(process.env.PORT ?? 3000),
  databaseUrl: requireEnv("DATABASE_URL"),
  requestBodyLimitBytes: Number(process.env.REQUEST_BODY_LIMIT_BYTES ?? 4096),
  ipRateLimitMax: Number(process.env.IP_RATE_LIMIT_MAX ?? 120),
  ipRateLimitWindowMs: Number(process.env.IP_RATE_LIMIT_WINDOW_MS ?? 60000),
  deviceRateLimitMax: Number(process.env.DEVICE_RATE_LIMIT_MAX ?? 10),
  deviceRateLimitWindowMs: Number(process.env.DEVICE_RATE_LIMIT_WINDOW_MS ?? 60000),
  deviceRequestMaxSkewMs: Number(process.env.DEVICE_REQUEST_MAX_SKEW_MS ?? 300000),
  deviceRetryAfterSeconds: Number(process.env.DEVICE_RETRY_AFTER_SECONDS ?? 60),
  deviceSecretEncryptionKey: requireEnv("DEVICE_SECRET_ENCRYPTION_KEY"),
  forwarderIntervalMs: Number(process.env.FORWARDER_INTERVAL_MS ?? 600000),
  forwarderBatchSize: Number(process.env.FORWARDER_BATCH_SIZE ?? 100),
  forwarderMaxRetries: Number(process.env.FORWARDER_MAX_RETRIES ?? 12),
  forwarderStaleProcessingMs: Number(process.env.FORWARDER_STALE_PROCESSING_MS ?? 900000),
  forwarderMaxConflictsBeforeDeadLetter: Number(process.env.FORWARDER_MAX_CONFLICTS_BEFORE_DEAD_LETTER ?? 3),
  convexUrl: requireEnv("CONVEX_URL"),
  convexIngestPath: process.env.CONVEX_INGEST_PATH ?? "/api/v1/readings/batch",
  convexHmacSecret: requireEnv("CONVEX_HMAC_SECRET"),
  convexCredentialVersion: Number(process.env.CONVEX_CREDENTIAL_VERSION ?? 1),
  convexMaxSkewMs: Number(process.env.CONVEX_MAX_SKEW_MS ?? 300000)
};
