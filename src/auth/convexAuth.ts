import { config } from "../config.js";
import type { ConvexBatchPayload } from "../types.js";
import { hmacSha256Hex, sha256Hex } from "./crypto.js";

export function buildConvexCanonicalRequest(rawBody: string, batchId: string, timestamp: string, credentialVersion: number): string {
  return [
    "POST",
    config.convexIngestPath,
    batchId,
    timestamp,
    String(credentialVersion),
    sha256Hex(rawBody)
  ].join("\n");
}

export function signConvexBatch(payload: ConvexBatchPayload): { rawBody: string; signature: string } {
  const rawBody = JSON.stringify(payload);
  const canonical = buildConvexCanonicalRequest(
    rawBody,
    payload.batchId,
    payload.timestamp,
    payload.credentialVersion
  );
  return {
    rawBody,
    signature: hmacSha256Hex(config.convexHmacSecret, canonical)
  };
}
