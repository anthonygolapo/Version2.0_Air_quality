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

export interface DeviceCredential {
  deviceId: string;
  status: "active" | "disabled";
  credentialVersion: number;
  secret: string;
  previousSecret?: string;
  previousCredentialVersion?: number;
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
  replay?: boolean;
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
