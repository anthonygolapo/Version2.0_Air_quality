import { z } from "zod";

const finite = (min: number, max: number) => z.number().finite().min(min).max(max);

// DGS2 values are transmitted by the firmware in ppb, not the datasheet's ppm.
export const DGS2_MAX_PPB = {
  co: 400_000, // 97x-100 CO: 400 ppm
  no2: 30_000, // 97x-500 NO2: 30 ppm
  o3: 20_000,  // 97x-400 O3: 20 ppm
  so2: 40_000  // 97x-600 SO2: 40 ppm
} as const;

export const readingSchema = z.object({
  deviceId: z.string().trim().min(1).max(64).regex(/^[A-Za-z0-9_-]+$/),
  sequenceNumber: z.number().int().nonnegative().max(2147483647),
  measuredAt: z.string().datetime({ offset: true }),
  pm1: finite(0, 5000),
  pm25: finite(0, 5000),
  pm10: finite(0, 5000),
  co: finite(0, DGS2_MAX_PPB.co),
  no2: finite(0, DGS2_MAX_PPB.no2),
  o3: finite(0, DGS2_MAX_PPB.o3),
  so2: finite(0, DGS2_MAX_PPB.so2),
  temperatureC: finite(-40, 85),
  humidityPercent: finite(0, 100),
  batteryVoltage: finite(0, 10),
  solarVoltage: finite(0, 48),
  signalStrength: z.number().int().min(-150).max(0),
  firmwareVersion: z.string().trim().min(1).max(64),
  alarmFlags: z.number().int().min(0).max(65535)
});

export const readingBatchSchema = z.object({
  deviceId: z.string().trim().min(1).max(64).regex(/^[A-Za-z0-9_-]+$/),
  batchId: z.string().trim().min(1).max(96).regex(/^[A-Za-z0-9_.:-]+$/),
  readings: z.array(readingSchema).min(1).max(100)
});

export type ReadingInput = z.infer<typeof readingSchema>;

export function zodIssuesToDetails(error: z.ZodError) {
  return error.issues.map((issue) => ({
    path: issue.path.join("."),
    message: issue.message
  }));
}
