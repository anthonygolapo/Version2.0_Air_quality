import { createHash, createHmac, timingSafeEqual } from "node:crypto";

export function sha256Hex(input: string): string {
  return createHash("sha256").update(input).digest("hex");
}

export function buildConvexCanonicalRequest(rawBody: string, batchId: string, timestamp: string, credentialVersion: string): string {
  return [
    "POST",
    "/api/v1/readings/batch",
    batchId,
    timestamp,
    credentialVersion,
    sha256Hex(rawBody)
  ].join("\n");
}

export function verifyConvexSignature(rawBody: string, headers: {
  batchId: string;
  timestamp: string;
  credentialVersion: string;
  signature: string;
}): boolean {
  const secret = process.env.CONVEX_HMAC_SECRET;
  if (!secret) {
    return false;
  }

  const canonical = buildConvexCanonicalRequest(rawBody, headers.batchId, headers.timestamp, headers.credentialVersion);
  const expected = createHmac("sha256", secret).update(canonical).digest("hex");
  const expectedBuffer = Buffer.from(expected, "hex");
  const actualBuffer = Buffer.from(headers.signature, "hex");
  if (expectedBuffer.length !== actualBuffer.length) {
    return false;
  }
  return timingSafeEqual(expectedBuffer, actualBuffer);
}

export function isFreshTimestamp(timestamp: string, maxSkewMs: number): boolean {
  const millis = Date.parse(timestamp);
  if (Number.isNaN(millis)) {
    return false;
  }
  return Math.abs(Date.now() - millis) <= maxSkewMs;
}
