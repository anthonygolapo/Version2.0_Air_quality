import assert from "node:assert/strict";
import test from "node:test";
import { DGS2_MAX_PPB, readingSchema } from "../src/validation/readings.js";

function reading(gases: Partial<Record<"co" | "no2" | "o3" | "so2", number>> = {}) {
  return {
    deviceId: "AQ01",
    sequenceNumber: 1,
    measuredAt: new Date().toISOString(),
    pm1: 1,
    pm25: 2,
    pm10: 3,
    co: gases.co ?? 0,
    no2: gases.no2 ?? 0,
    o3: gases.o3 ?? 0,
    so2: gases.so2 ?? 0,
    temperatureC: 25,
    humidityPercent: 50,
    batteryVoltage: 4.1,
    solarVoltage: 5.2,
    signalStrength: -55,
    firmwareVersion: "test-1.0.0",
    alarmFlags: 0
  };
}

test("accepts each DGS2 sensor's maximum ppb reading", () => {
  assert.equal(readingSchema.safeParse(reading(DGS2_MAX_PPB)).success, true);
});

test("rejects DGS2 readings above their sensor range", () => {
  for (const gas of Object.keys(DGS2_MAX_PPB) as Array<keyof typeof DGS2_MAX_PPB>) {
    const result = readingSchema.safeParse(reading({ [gas]: DGS2_MAX_PPB[gas] + 1 }));
    assert.equal(result.success, false, `${gas} above its range should be rejected`);
  }
});
