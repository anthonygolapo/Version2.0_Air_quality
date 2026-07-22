import crypto from "node:crypto";
import fs from "node:fs";

const configPath = new URL("../firmware/main/config.h", import.meta.url);
if (!fs.existsSync(configPath)) {
  throw new Error("Missing firmware/main/config.h. Create it from config.example.h first.");
}

const configText = fs.readFileSync(configPath, "utf8");

function readMacro(name) {
  const match = configText.match(new RegExp(`^#define\\s+${name}\\s+"?([^"\\r\\n]+)"?`, "m"));
  if (!match) {
    throw new Error(`Missing ${name} in firmware/main/config.h`);
  }
  return match[1].trim();
}

const deviceId = readMacro("DEVICE_ID");
const deviceSecret = readMacro("DEVICE_SECRET");
const credentialVersion = readMacro("DEVICE_CREDENTIAL_VERSION");
const serverUrl = readMacro("NODE_SERVER_URL");

if (!serverUrl.startsWith("https://")) {
  throw new Error("NODE_SERVER_URL must use HTTPS.");
}

const sequenceNumber = 0;
const timestamp = new Date().toISOString();
const batchId = `${deviceId}-api-check-${Date.now()}-${crypto.randomUUID()}`;
const payload = {
  deviceId,
  batchId,
  readings: [{
    deviceId,
    sequenceNumber,
    measuredAt: timestamp,
    pm1: 4.2,
    pm25: 7.8,
    pm10: 11.3,
    co: 3528,
    no2: 408,
    o3: 375,
    so2: 5,
    temperatureC: 25.8,
    humidityPercent: 57,
    batteryVoltage: 4.08,
    solarVoltage: 6.35,
    signalStrength: -61,
    firmwareVersion: "synthetic-api-check",
    alarmFlags: 0
  }]
};

const rawBody = JSON.stringify(payload);
const payloadHash = crypto.createHash("sha256").update(rawBody).digest("hex");
const canonical = [
  "POST",
  "/api/v1/readings",
  deviceId,
  batchId,
  timestamp,
  credentialVersion,
  payloadHash
].join("\n");
const signature = crypto.createHmac("sha256", deviceSecret).update(canonical).digest("hex");

const response = await fetch(serverUrl, {
  method: "POST",
  headers: {
    "content-type": "application/json",
    "x-device-id": deviceId,
    "x-batch-id": batchId,
    "x-timestamp": timestamp,
    "x-credential-version": credentialVersion,
    "x-signature": signature
  },
  body: rawBody
});

let result;
try {
  result = await response.json();
} catch {
  throw new Error(`API returned HTTP ${response.status} with a non-JSON response.`);
}

console.log(JSON.stringify({
  httpStatus: response.status,
  status: result.status,
  code: result.code,
  message: result.message,
  acceptedSequenceNumbers: result.acceptedSequenceNumbers ?? [],
  duplicateSequenceNumbers: result.duplicateSequenceNumbers ?? [],
  conflictingSequenceNumbers: result.conflictingSequenceNumbers ?? [],
  rejectedSequenceNumbers: result.rejectedSequenceNumbers ?? [],
  retryableSequenceNumbers: result.retryableSequenceNumbers ?? []
}, null, 2));

if (!response.ok || result.code !== "batch_stored") {
  process.exitCode = 1;
}
