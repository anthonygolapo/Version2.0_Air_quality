import { config } from "../config.js";
import type { DeviceCredential } from "../types.js";
import { hmacSha256Hex, sha256Hex, timingSafeEqualHex } from "./crypto.js";

export interface DeviceHeaders {
  deviceId: string;
  batchId: string;
  timestamp: string;
  credentialVersion: string;
  signature: string;
}

export function buildDeviceCanonicalRequest(rawBody: string, headers: DeviceHeaders): string {
  return [
    "POST",
    "/api/v1/readings",
    headers.deviceId,
    headers.batchId,
    headers.timestamp,
    headers.credentialVersion,
    sha256Hex(rawBody)
  ].join("\n");
}

export function verifyDeviceTimestamp(timestamp: string): boolean {
  const millis = Date.parse(timestamp);
  if (Number.isNaN(millis)) {
    return false;
  }
  return Math.abs(Date.now() - millis) <= config.deviceRequestMaxSkewMs;
}

export function selectDeviceSecret(device: DeviceCredential, credentialVersion: number): string | null {
  if (device.credentialVersion === credentialVersion) {
    return device.secret;
  }
  if (device.previousCredentialVersion === credentialVersion && device.previousSecret) {
    return device.previousSecret;
  }
  return null;
}

export function verifyDeviceSignature(rawBody: string, headers: DeviceHeaders, device: DeviceCredential): boolean {
  const credentialVersion = Number(headers.credentialVersion);
  if (!Number.isInteger(credentialVersion)) {
    return false;
  }

  const secret = selectDeviceSecret(device, credentialVersion);
  if (!secret) {
    return false;
  }

  const canonical = buildDeviceCanonicalRequest(rawBody, headers);
  const expected = hmacSha256Hex(secret, canonical);
  return timingSafeEqualHex(expected, headers.signature);
}
