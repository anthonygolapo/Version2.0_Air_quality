import { config } from "../config.js";
import type { ConvexBatchPayload, ConvexBatchResponse, QueueRecord } from "../types.js";
import { signConvexBatch } from "../auth/convexAuth.js";

export class ConvexRequestError extends Error {
  constructor(
    message: string,
    public readonly transient: boolean,
    public readonly retryAfterMs?: number
  ) {
    super(message);
  }
}

export async function sendBatchToConvex(records: QueueRecord[], batchId: string): Promise<ConvexBatchResponse> {
  const payload: ConvexBatchPayload = {
    batchId,
    timestamp: new Date().toISOString(),
    credentialVersion: config.convexCredentialVersion,
    records: records.map((record) => ({
      deviceId: record.deviceId,
      sequenceNumber: record.sequenceNumber,
      measuredAt: record.measuredAt,
      receivedAt: record.receivedAt,
      payloadHash: record.payloadHash,
      pm1: record.pm1,
      pm25: record.pm25,
      pm10: record.pm10,
      co: record.co,
      no2: record.no2,
      o3: record.o3,
      so2: record.so2,
      temperatureC: record.temperatureC,
      humidityPercent: record.humidityPercent,
      batteryVoltage: record.batteryVoltage,
      solarVoltage: record.solarVoltage,
      signalStrength: record.signalStrength,
      firmwareVersion: record.firmwareVersion,
      alarmFlags: record.alarmFlags
    }))
  };

  const { rawBody, signature } = signConvexBatch(payload);
  const response = await fetch(`${config.convexUrl}${config.convexIngestPath}`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-batch-id": payload.batchId,
      "x-timestamp": payload.timestamp,
      "x-credential-version": String(payload.credentialVersion),
      "x-signature": signature
    },
    body: rawBody
  });

  const retryAfterHeader = response.headers.get("retry-after");
  const retryAfterMs = retryAfterHeader ? Number(retryAfterHeader) * 1000 : undefined;
  const text = await response.text();
  const parsed = text ? JSON.parse(text) : {};

  if (!response.ok) {
    const transient = response.status === 429 || response.status >= 500 || response.status === 503;
    throw new ConvexRequestError(`Convex request failed with status ${response.status}`, transient, retryAfterMs);
  }

  return parsed as ConvexBatchResponse;
}
