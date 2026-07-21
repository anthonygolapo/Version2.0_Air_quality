import { randomUUID } from "node:crypto";
import type { FastifyInstance } from "fastify";
import { config } from "../config.js";
import { verifyDeviceSignature, verifyDeviceTimestamp, type DeviceHeaders } from "../auth/deviceAuth.js";
import { sendStructured } from "../http/responses.js";
import { checkRateLimit } from "../rateLimit.js";
import { ConvexRequestError, sendBatchToConvex } from "../services/convexClient.js";
import { zodIssuesToDetails, readingBatchSchema } from "../validation/readings.js";

function headerValue(value: string | string[] | undefined): string {
  return Array.isArray(value) ? value[0] ?? "" : value ?? "";
}

export async function readingsRoutes(fastify: FastifyInstance): Promise<void> {
  fastify.post("/api/v1/readings", async (request, reply) => {
    const rawBody = typeof request.body === "string" ? request.body : "";
    if (!checkRateLimit(`ip:${request.ip}`, config.ipRateLimitMax, config.ipRateLimitWindowMs)) {
      reply.header("retry-after", String(config.deviceRetryAfterSeconds));
      return sendStructured(reply, 429, "error", "rate_limited", "IP rate limit exceeded.", true);
    }

    const headers: DeviceHeaders = {
      deviceId: headerValue(request.headers["x-device-id"]),
      batchId: headerValue(request.headers["x-batch-id"]),
      timestamp: headerValue(request.headers["x-timestamp"]),
      credentialVersion: headerValue(request.headers["x-credential-version"]),
      signature: headerValue(request.headers["x-signature"])
    };

    if (!headers.deviceId || !headers.batchId || !headers.timestamp || !headers.credentialVersion || !headers.signature) {
      return sendStructured(reply, 400, "error", "invalid_request", "Missing required authentication headers.", false);
    }
    if (!verifyDeviceTimestamp(headers.timestamp)) {
      return sendStructured(reply, 401, "error", "authentication_failed", "Request timestamp is expired or invalid.", false);
    }
    if (!checkRateLimit(`device:${headers.deviceId}`, config.deviceRateLimitMax, config.deviceRateLimitWindowMs)) {
      reply.header("retry-after", String(config.deviceRetryAfterSeconds));
      return sendStructured(reply, 429, "error", "rate_limited", "Device rate limit exceeded.", true);
    }

    const device = config.deviceCredentials.get(headers.deviceId);
    if (!device) {
      return sendStructured(reply, 401, "error", "authentication_failed", "Unknown device.", false);
    }
    if (device.status === "disabled") {
      return sendStructured(reply, 403, "error", "device_disabled", "Device is disabled.", false);
    }
    if (!verifyDeviceSignature(rawBody, headers, device)) {
      return sendStructured(reply, 401, "error", "authentication_failed", "Signature verification failed.", false);
    }

    let payload: unknown;
    try {
      payload = JSON.parse(rawBody);
    } catch {
      return sendStructured(reply, 400, "error", "invalid_request", "Request body must be valid JSON.", false);
    }

    const parsed = readingBatchSchema.safeParse(payload);
    if (!parsed.success) {
      return sendStructured(reply, 422, "error", "invalid_sensor_data", "Batch failed validation.", false, {
        details: zodIssuesToDetails(parsed.error)
      });
    }
    if (parsed.data.readings.length > config.deviceBatchSize) {
      return sendStructured(reply, 413, "error", "batch_too_large", `Batch cannot exceed ${config.deviceBatchSize} readings.`, false);
    }
    if (parsed.data.deviceId !== headers.deviceId || parsed.data.batchId !== headers.batchId) {
      return sendStructured(reply, 400, "error", "invalid_request", "Signed headers do not match the batch payload.", false);
    }

    const sequenceNumbers = new Set<number>();
    for (const reading of parsed.data.readings) {
      if (reading.deviceId !== headers.deviceId) {
        return sendStructured(reply, 400, "error", "invalid_request", "Every reading must belong to the authenticated device.", false);
      }
      if (sequenceNumbers.has(reading.sequenceNumber)) {
        return sendStructured(reply, 400, "error", "invalid_request", "Batch contains duplicate sequence numbers.", false);
      }
      sequenceNumbers.add(reading.sequenceNumber);
    }

    const receivedAt = new Date().toISOString();
    try {
      // A fresh forwarding ID lets a retried device batch be resolved through
      // Convex's deviceId + sequenceNumber deduplication contract.
      const convexBatchId = `${headers.deviceId}:${headers.batchId}:${randomUUID()}`;
      const result = await sendBatchToConvex(parsed.data.readings, convexBatchId, receivedAt);
      const rejectedSequenceNumbers = result.rejected
        .filter((item) => !item.retryable)
        .map((item) => item.sequenceNumber);
      const retryableSequenceNumbers = result.rejected
        .filter((item) => item.retryable)
        .map((item) => item.sequenceNumber);

      return sendStructured(reply, 200, "ok", "batch_stored", "Convex processed the reading batch.", false, {
        deviceId: headers.deviceId,
        batchId: headers.batchId,
        acceptedSequenceNumbers: result.acceptedSequenceNumbers,
        duplicateSequenceNumbers: result.duplicateSequenceNumbers,
        conflictingSequenceNumbers: result.conflictingSequenceNumbers,
        rejectedSequenceNumbers,
        retryableSequenceNumbers,
        acceptedCount: result.acceptedCount,
        duplicateCount: result.duplicateCount,
        conflictingCount: result.conflictingCount,
        rejectedCount: result.rejectedCount
      });
    } catch (error) {
      const transient = error instanceof ConvexRequestError ? error.transient : true;
      request.log.error({ err: error, deviceId: headers.deviceId, batchId: headers.batchId }, "Direct Convex forwarding failed");
      if (error instanceof ConvexRequestError && error.retryAfterMs) {
        reply.header("retry-after", String(Math.ceil(error.retryAfterMs / 1000)));
      }
      return sendStructured(
        reply,
        transient ? 503 : 502,
        "error",
        "convex_forwarding_failed",
        "Convex did not confirm storage; retain the local batch and retry.",
        transient
      );
    }
  });
}
