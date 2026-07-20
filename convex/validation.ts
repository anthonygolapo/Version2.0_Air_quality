import { v } from "convex/values";

export const readingValidator = v.object({
  deviceId: v.string(),
  sequenceNumber: v.number(),
  measuredAt: v.string(),
  receivedAt: v.string(),
  payloadHash: v.string(),
  pm1: v.number(),
  pm25: v.number(),
  pm10: v.number(),
  co: v.number(),
  no2: v.number(),
  o3: v.number(),
  so2: v.number(),
  temperatureC: v.number(),
  humidityPercent: v.number(),
  batteryVoltage: v.number(),
  solarVoltage: v.number(),
  signalStrength: v.number(),
  firmwareVersion: v.string(),
  alarmFlags: v.number()
});

export function validateReadingSemantics(reading: {
  deviceId: string;
  sequenceNumber: number;
  measuredAt: string;
  receivedAt: string;
  payloadHash: string;
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
}): string | null {
  if (!/^[A-Za-z0-9_-]{1,64}$/.test(reading.deviceId)) return "invalid deviceId";
  if (!Number.isInteger(reading.sequenceNumber) || reading.sequenceNumber < 0) return "invalid sequenceNumber";
  if (Number.isNaN(Date.parse(reading.measuredAt))) return "invalid measuredAt";
  if (Number.isNaN(Date.parse(reading.receivedAt))) return "invalid receivedAt";
  if (!/^[a-f0-9]{64}$/.test(reading.payloadHash)) return "invalid payloadHash";
  if (reading.pm1 < 0 || reading.pm1 > 5000) return "invalid pm1";
  if (reading.pm25 < 0 || reading.pm25 > 5000) return "invalid pm25";
  if (reading.pm10 < 0 || reading.pm10 > 5000) return "invalid pm10";
  if (reading.co < 0 || reading.co > 1000) return "invalid co";
  if (reading.no2 < 0 || reading.no2 > 100) return "invalid no2";
  if (reading.o3 < 0 || reading.o3 > 100) return "invalid o3";
  if (reading.so2 < 0 || reading.so2 > 100) return "invalid so2";
  if (reading.temperatureC < -40 || reading.temperatureC > 85) return "invalid temperatureC";
  if (reading.humidityPercent < 0 || reading.humidityPercent > 100) return "invalid humidityPercent";
  if (reading.batteryVoltage < 0 || reading.batteryVoltage > 10) return "invalid batteryVoltage";
  if (reading.solarVoltage < 0 || reading.solarVoltage > 48) return "invalid solarVoltage";
  if (!Number.isInteger(reading.signalStrength) || reading.signalStrength < -150 || reading.signalStrength > 0) return "invalid signalStrength";
  if (!Number.isInteger(reading.alarmFlags) || reading.alarmFlags < 0 || reading.alarmFlags > 65535) return "invalid alarmFlags";
  return null;
}
