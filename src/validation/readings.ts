import { z } from "zod";

const finite = (min: number, max: number) => z.number().finite().min(min).max(max);

export const readingSchema = z.object({
  deviceId: z.string().trim().min(1).max(64).regex(/^[A-Za-z0-9_-]+$/),
  sequenceNumber: z.number().int().nonnegative().max(2147483647),
  measuredAt: z.string().datetime({ offset: true }),
  pm1: finite(0, 5000),
  pm25: finite(0, 5000),
  pm10: finite(0, 5000),
  co: finite(0, 1000),
  no2: finite(0, 100),
  o3: finite(0, 100),
  so2: finite(0, 100),
  temperatureC: finite(-40, 85),
  humidityPercent: finite(0, 100),
  batteryVoltage: finite(0, 10),
  solarVoltage: finite(0, 48),
  signalStrength: z.number().int().min(-150).max(0),
  firmwareVersion: z.string().trim().min(1).max(64),
  alarmFlags: z.number().int().min(0).max(65535)
});

export type ReadingInput = z.infer<typeof readingSchema>;

export function zodIssuesToDetails(error: z.ZodError) {
  return error.issues.map((issue) => ({
    path: issue.path.join("."),
    message: issue.message
  }));
}
