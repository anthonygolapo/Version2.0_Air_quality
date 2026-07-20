import { mutation } from "./_generated/server";
import { v } from "convex/values";
import { readingValidator, validateReadingSemantics } from "./validation";

export const ingestBatch = mutation({
  args: {
    batchId: v.string(),
    requestHash: v.string(),
    timestamp: v.string(),
    records: v.array(readingValidator)
  },
  handler: async (ctx, args) => {
    const existingReceipt = await ctx.db
      .query("batchReceipts")
      .withIndex("by_batch_id", (q) => q.eq("batchId", args.batchId))
      .unique();

    if (existingReceipt) {
      if (existingReceipt.requestHash !== args.requestHash) {
        return {
          replay: true,
          acceptedCount: 0,
          duplicateCount: 0,
          rejectedCount: 0,
          conflictingCount: 0,
          acceptedSequenceNumbers: [],
          duplicateSequenceNumbers: [],
          conflictingSequenceNumbers: [],
          rejected: []
        };
      }

      return {
        replay: true,
        acceptedCount: 0,
        duplicateCount: 0,
        rejectedCount: 0,
        conflictingCount: 0,
        acceptedSequenceNumbers: [],
        duplicateSequenceNumbers: [],
        conflictingSequenceNumbers: [],
        rejected: []
      };
    }

    await ctx.db.insert("batchReceipts", {
      batchId: args.batchId,
      requestHash: args.requestHash,
      timestamp: args.timestamp,
      createdAt: new Date().toISOString()
    });

    const acceptedSequenceNumbers: number[] = [];
    const duplicateSequenceNumbers: number[] = [];
    const conflictingSequenceNumbers: number[] = [];
    const rejected: Array<{ deviceId: string; sequenceNumber: number; reason: string; retryable: boolean }> = [];

    for (const record of args.records) {
      const semanticFailure = validateReadingSemantics(record);
      if (semanticFailure) {
        rejected.push({
          deviceId: record.deviceId,
          sequenceNumber: record.sequenceNumber,
          reason: semanticFailure,
          retryable: false
        });
        continue;
      }

      const deviceSequenceKey = `${record.deviceId}:${record.sequenceNumber}`;
      const existing = await ctx.db
        .query("readings")
        .withIndex("by_device_sequence_key", (q) => q.eq("deviceSequenceKey", deviceSequenceKey))
        .unique();

      if (existing) {
        if (existing.payloadHash === record.payloadHash) {
          duplicateSequenceNumbers.push(record.sequenceNumber);
        } else {
          conflictingSequenceNumbers.push(record.sequenceNumber);
        }
        continue;
      }

      await ctx.db.insert("readings", {
        ...record,
        deviceSequenceKey,
        ingestedAt: new Date().toISOString()
      });
      acceptedSequenceNumbers.push(record.sequenceNumber);

      const existingDevice = await ctx.db
        .query("devices")
        .withIndex("by_device_id", (q) => q.eq("deviceId", record.deviceId))
        .unique();

      const updatedAt = new Date().toISOString();
      if (existingDevice) {
        await ctx.db.patch(existingDevice._id, {
          lastSeenAt: record.measuredAt,
          lastSequenceNumber: record.sequenceNumber,
          updatedAt
        });
      } else {
        await ctx.db.insert("devices", {
          deviceId: record.deviceId,
          lastSeenAt: record.measuredAt,
          lastSequenceNumber: record.sequenceNumber,
          updatedAt
        });
      }
    }

    return {
      replay: false,
      acceptedCount: acceptedSequenceNumbers.length,
      duplicateCount: duplicateSequenceNumbers.length,
      rejectedCount: rejected.length,
      conflictingCount: conflictingSequenceNumbers.length,
      acceptedSequenceNumbers,
      duplicateSequenceNumbers,
      conflictingSequenceNumbers,
      rejected
    };
  }
});
