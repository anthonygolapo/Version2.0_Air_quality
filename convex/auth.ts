const encoder = new TextEncoder();

function bytesToHex(bytes: ArrayBuffer): string {
  return Array.from(new Uint8Array(bytes), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function constantTimeEqualHex(expected: string, actual: string): boolean {
  if (!/^[0-9a-f]{64}$/i.test(actual)) {
    return false;
  }

  let difference = expected.length ^ actual.length;
  for (let index = 0; index < expected.length; index += 1) {
    difference |= expected.charCodeAt(index) ^ (actual.charCodeAt(index) || 0);
  }
  return difference === 0;
}

export async function sha256Hex(input: string): Promise<string> {
  return bytesToHex(await crypto.subtle.digest("SHA-256", encoder.encode(input)));
}

export async function buildConvexCanonicalRequest(rawBody: string, batchId: string, timestamp: string, credentialVersion: string): Promise<string> {
  return [
    "POST",
    "/api/v1/readings/batch",
    batchId,
    timestamp,
    credentialVersion,
    await sha256Hex(rawBody)
  ].join("\n");
}

export async function verifyConvexSignature(rawBody: string, headers: {
  batchId: string;
  timestamp: string;
  credentialVersion: string;
  signature: string;
}): Promise<boolean> {
  const secret = process.env.CONVEX_HMAC_SECRET;
  if (!secret) {
    return false;
  }

  const canonical = await buildConvexCanonicalRequest(rawBody, headers.batchId, headers.timestamp, headers.credentialVersion);
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const expected = bytesToHex(await crypto.subtle.sign("HMAC", key, encoder.encode(canonical)));
  return constantTimeEqualHex(expected, headers.signature);
}

export function isFreshTimestamp(timestamp: string, maxSkewMs: number): boolean {
  const millis = Date.parse(timestamp);
  if (Number.isNaN(millis)) {
    return false;
  }
  return Math.abs(Date.now() - millis) <= maxSkewMs;
}
