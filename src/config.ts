import dotenv from "dotenv";

dotenv.config();

function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

function parseDeviceCredentials(): Map<string, import("./types.js").DeviceCredential> {
  let value: unknown;
  try {
    value = JSON.parse(requireEnv("DEVICE_CREDENTIALS_JSON"));
  } catch {
    throw new Error("DEVICE_CREDENTIALS_JSON must be valid JSON.");
  }

  if (!Array.isArray(value) || value.length === 0) {
    throw new Error("DEVICE_CREDENTIALS_JSON must contain at least one device credential.");
  }

  const credentials = new Map<string, import("./types.js").DeviceCredential>();
  for (const entry of value) {
    if (
      typeof entry !== "object" || entry === null ||
      typeof (entry as { deviceId?: unknown }).deviceId !== "string" ||
      typeof (entry as { secret?: unknown }).secret !== "string" ||
      !Number.isInteger((entry as { credentialVersion?: unknown }).credentialVersion) ||
      !["active", "disabled"].includes(String((entry as { status?: unknown }).status))
    ) {
      throw new Error("DEVICE_CREDENTIALS_JSON contains an invalid device entry.");
    }

    const credential = entry as import("./types.js").DeviceCredential;
    if (credentials.has(credential.deviceId)) {
      throw new Error(`Duplicate device credential: ${credential.deviceId}`);
    }
    credentials.set(credential.deviceId, credential);
  }
  return credentials;
}

export const config = {
  nodeEnv: process.env.NODE_ENV ?? "development",
  host: process.env.HOST ?? "0.0.0.0",
  port: Number(process.env.PORT ?? 3000),
  requestBodyLimitBytes: Number(process.env.REQUEST_BODY_LIMIT_BYTES ?? 32768),
  ipRateLimitMax: Number(process.env.IP_RATE_LIMIT_MAX ?? 120),
  ipRateLimitWindowMs: Number(process.env.IP_RATE_LIMIT_WINDOW_MS ?? 60000),
  deviceRateLimitMax: Number(process.env.DEVICE_RATE_LIMIT_MAX ?? 10),
  deviceRateLimitWindowMs: Number(process.env.DEVICE_RATE_LIMIT_WINDOW_MS ?? 60000),
  deviceRequestMaxSkewMs: Number(process.env.DEVICE_REQUEST_MAX_SKEW_MS ?? 300000),
  deviceRetryAfterSeconds: Number(process.env.DEVICE_RETRY_AFTER_SECONDS ?? 60),
  deviceBatchSize: Number(process.env.DEVICE_BATCH_SIZE ?? 10),
  deviceCredentials: parseDeviceCredentials(),
  convexUrl: requireEnv("CONVEX_URL"),
  convexIngestPath: process.env.CONVEX_INGEST_PATH ?? "/api/v1/readings/batch",
  convexHmacSecret: requireEnv("CONVEX_HMAC_SECRET"),
  convexCredentialVersion: Number(process.env.CONVEX_CREDENTIAL_VERSION ?? 1),
  convexMaxSkewMs: Number(process.env.CONVEX_MAX_SKEW_MS ?? 300000)
};
