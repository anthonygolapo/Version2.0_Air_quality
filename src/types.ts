export type QueueStatus = "pending" | "processing" | "sent" | "failed";

export interface AirQualityReading {
  deviceId: string;
  sequenceNumber: number;
  measuredAt: string;
  pm1: number;
  pm25: number;
  pm10: number;
  co: number;
  no2: number;
  o3: number;
  so2: number;
  temperatureC: number;
  humidityPercent: number;
  batteryVoltage: number;
  solarVoltage: number;
  signalStrength: number;
  firmwareVersion: string;
  alarmFlags: number;
}

export interface QueueRecord extends AirQualityReading {
  id: string;
  payloadHash: string;
  receivedAt: string;
  status: QueueStatus;
  retryCount: number;
  nextRetryAt: string | null;
  lastError: string | null;
  claimedBy: string | null;
  claimedAt: string | null;
  createdAt: string;
  sentAt: string | null;
}

export interface DeviceRecord {
  deviceId: string;
  status: "active" | "disabled";
  credentialVersion: number;
  secretCiphertext: string;
  previousSecretCiphertext: string | null;
  previousCredentialVersion: number | null;
}

export interface StructuredResponse {
  status: string;
  code: string;
  message: string;
  retryable: boolean;
  serverTime: string;
}

export interface ConvexBatchPayload {
  batchId: string;
  timestamp: string;
  credentialVersion: number;
  records: Array<AirQualityReading & { receivedAt: string; payloadHash: string }>;
}

export interface ConvexBatchResponse {
  acceptedCount: number;
  duplicateCount: number;
  rejectedCount: number;
  conflictingCount: number;
  acceptedSequenceNumbers: number[];
  duplicateSequenceNumbers: number[];
  conflictingSequenceNumbers: number[];
  rejected: Array<{
    deviceId: string;
    sequenceNumber: number;
    reason: string;
    retryable: boolean;
  }>;
}
