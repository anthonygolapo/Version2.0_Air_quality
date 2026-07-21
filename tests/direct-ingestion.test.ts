import assert from "node:assert/strict";
import crypto from "node:crypto";
import test from "node:test";

const deviceSecret = "test-device-secret-not-for-production";
process.env.NODE_ENV = "test";
process.env.DEVICE_CREDENTIALS_JSON = JSON.stringify([{
  deviceId: "AQ01",
  status: "active",
  credentialVersion: 1,
  secret: deviceSecret
}]);
process.env.CONVEX_URL = "https://example.convex.site";
process.env.CONVEX_HMAC_SECRET = "test-convex-secret-not-for-production";
process.env.CONVEX_CREDENTIAL_VERSION = "1";
process.env.DEVICE_BATCH_SIZE = "10";

const { buildApp } = await import("../src/app.js");

function createReading(sequenceNumber: number) {
  return {
    deviceId: "AQ01",
    sequenceNumber,
    measuredAt: new Date().toISOString(),
    pm1: 1,
    pm25: 2,
    pm10: 3,
    co: 0,
    no2: 0,
    o3: 0,
    so2: 0,
    temperatureC: 25,
    humidityPercent: 60,
    batteryVoltage: 4.1,
    solarVoltage: 5.2,
    signalStrength: -55,
    firmwareVersion: "test-1.0.0",
    alarmFlags: 0
  };
}

function signDeviceBody(rawBody: string, batchId: string, timestamp: string): string {
  const payloadHash = crypto.createHash("sha256").update(rawBody).digest("hex");
  const canonical = ["POST", "/api/v1/readings", "AQ01", batchId, timestamp, "1", payloadHash].join("\n");
  return crypto.createHmac("sha256", deviceSecret).update(canonical).digest("hex");
}

test("authenticated batches are validated and forwarded directly to Convex", async () => {
  const app = await buildApp();
  const batchId = "AQ01-0000000001-0000000002";
  const payload = { deviceId: "AQ01", batchId, readings: [createReading(1), createReading(2)] };
  const rawBody = JSON.stringify(payload);
  const timestamp = new Date().toISOString();
  let forwardedBody: Record<string, unknown> | undefined;

  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (_input, init) => {
    forwardedBody = JSON.parse(String(init?.body)) as Record<string, unknown>;
    return new Response(JSON.stringify({
      replay: false,
      acceptedCount: 2,
      duplicateCount: 0,
      rejectedCount: 0,
      conflictingCount: 0,
      acceptedSequenceNumbers: [1, 2],
      duplicateSequenceNumbers: [],
      conflictingSequenceNumbers: [],
      rejected: []
    }), { status: 200, headers: { "content-type": "application/json" } });
  };

  try {
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/readings",
      headers: {
        "content-type": "application/json",
        "x-device-id": "AQ01",
        "x-batch-id": batchId,
        "x-timestamp": timestamp,
        "x-credential-version": "1",
        "x-signature": signDeviceBody(rawBody, batchId, timestamp)
      },
      payload: rawBody
    });

    assert.equal(response.statusCode, 200);
    assert.deepEqual(response.json().acceptedSequenceNumbers, [1, 2]);
    assert.equal((forwardedBody?.records as unknown[]).length, 2);
  } finally {
    globalThis.fetch = originalFetch;
    await app.close();
  }
});

test("invalid device signatures are rejected before forwarding", async () => {
  const app = await buildApp();
  const batchId = "AQ01-0000000003-0000000003";
  const payload = { deviceId: "AQ01", batchId, readings: [createReading(3)] };
  const response = await app.inject({
    method: "POST",
    url: "/api/v1/readings",
    headers: {
      "content-type": "application/json",
      "x-device-id": "AQ01",
      "x-batch-id": batchId,
      "x-timestamp": new Date().toISOString(),
      "x-credential-version": "1",
      "x-signature": "00".repeat(32)
    },
    payload: JSON.stringify(payload)
  });

  assert.equal(response.statusCode, 401);
  assert.equal(response.json().code, "authentication_failed");
  await app.close();
});
