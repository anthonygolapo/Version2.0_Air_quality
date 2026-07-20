import { defineSchema, defineTable } from "convex/server";
import { v } from "convex/values";

export default defineSchema({
  readings: defineTable({
    deviceId: v.string(),
    sequenceNumber: v.number(),
    deviceSequenceKey: v.string(),
    payloadHash: v.string(),
    measuredAt: v.string(),
    receivedAt: v.string(),
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
    alarmFlags: v.number(),
    ingestedAt: v.string()
  }).index("by_device_sequence_key", ["deviceSequenceKey"]),

  devices: defineTable({
    deviceId: v.string(),
    lastSeenAt: v.string(),
    lastSequenceNumber: v.number(),
    updatedAt: v.string()
  }).index("by_device_id", ["deviceId"]),

  batchReceipts: defineTable({
    batchId: v.string(),
    requestHash: v.string(),
    timestamp: v.string(),
    createdAt: v.string()
  }).index("by_batch_id", ["batchId"])
});
