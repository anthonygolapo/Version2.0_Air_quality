import type { FastifyInstance } from "fastify";
import { config } from "../config.js";
import { verifyDeviceSignature, verifyDeviceTimestamp, type DeviceHeaders } from "../auth/deviceAuth.js";
import { sendStructured } from "../http/responses.js";
import { DuplicateReadingError } from "../db/queueRepository.js";
import { zodIssuesToDetails, readingSchema } from "../validation/readings.js";
import { sha256Hex } from "../auth/crypto.js";

function headerValue(value: string | string[] | undefined): string {
  return Array.isArray(value) ? value[0] ?? "" : value ?? "";
}

export async function readingsRoutes(fastify: FastifyInstance): Promise<void> {
  fastify.post("/api/v1/readings", async (request, reply) => {
    const ip = request.ip;
    const rawBody = typeof request.body === "string" ? request.body : "";

    if (!(await fastify.rateLimits.checkAndIncrement("ip", ip, config.ipRateLimitMax, config.ipRateLimitWindowMs))) {
      reply.header("retry-after", String(config.deviceRetryAfterSeconds));
      return sendStructured(reply, 429, "error", "rate_limited", "IP rate limit exceeded.", true);
    }

    const headers: DeviceHeaders = {
      deviceId: headerValue(request.headers["x-device-id"]),
      sequenceNumber: headerValue(request.headers["x-sequence-number"]),
      timestamp: headerValue(request.headers["x-timestamp"]),
      credentialVersion: headerValue(request.headers["x-credential-version"]),
      signature: headerValue(request.headers["x-signature"])
    };

    if (!headers.deviceId || !headers.sequenceNumber || !headers.timestamp || !headers.credentialVersion || !headers.signature) {
      return sendStructured(reply, 400, "error", "invalid_request", "Missing required authentication headers.", false);
    }

    if (!verifyDeviceTimestamp(headers.timestamp)) {
      return sendStructured(reply, 401, "error", "authentication_failed", "Request timestamp is expired or invalid.", false);
    }

    if (!(await fastify.rateLimits.checkAndIncrement("device", headers.deviceId, config.deviceRateLimitMax, config.deviceRateLimitWindowMs))) {
      reply.header("retry-after", String(config.deviceRetryAfterSeconds));
      return sendStructured(reply, 429, "error", "rate_limited", "Device rate limit exceeded.", true);
    }

    const device = await fastify.devices.findByDeviceId(headers.deviceId);
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

    const parsed = readingSchema.safeParse(payload);
    if (!parsed.success) {
      return sendStructured(reply, 422, "error", "invalid_sensor_data", "Payload failed validation.", false, {
        details: zodIssuesToDetails(parsed.error)
      });
    }

    if (parsed.data.deviceId !== headers.deviceId || String(parsed.data.sequenceNumber) !== headers.sequenceNumber) {
      return sendStructured(reply, 400, "error", "invalid_request", "Signed headers do not match the payload.", false);
    }

    const payloadHash = sha256Hex(rawBody);
    const receivedAt = new Date().toISOString();

    try {
      await fastify.queue.enqueue(parsed.data, payloadHash, rawBody, receivedAt);
      return sendStructured(reply, 202, "accepted", "queued_successfully", "Reading was committed to persistent queue storage.", false, {
        deviceId: parsed.data.deviceId,
        sequenceNumber: parsed.data.sequenceNumber,
        queuedAt: receivedAt
      });
    } catch (error) {
      if (error instanceof DuplicateReadingError) {
        return sendStructured(
          reply,
          409,
          "error",
          error.conflicting ? "conflicting_duplicate" : "duplicate_reading",
          error.conflicting ? "Sequence number already exists with a different payload." : "Reading already queued.",
          false
        );
      }

      request.log.error({ err: error, deviceId: headers.deviceId }, "Persistent queue write failed");
      return sendStructured(reply, 503, "error", "temporary_storage_failure", "Persistent queue storage is temporarily unavailable.", true);
    }
  });
}
