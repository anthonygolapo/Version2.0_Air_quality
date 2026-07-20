import { mutation } from "./_generated/server";
import { v } from "convex/values";

export const upsertLastSeen = mutation({
  args: {
    deviceId: v.string(),
    measuredAt: v.string(),
    sequenceNumber: v.number()
  },
  handler: async (ctx, args) => {
    const existing = await ctx.db
      .query("devices")
      .withIndex("by_device_id", (q) => q.eq("deviceId", args.deviceId))
      .unique();

    const updatedAt = new Date().toISOString();
    if (existing) {
      await ctx.db.patch(existing._id, {
        lastSeenAt: args.measuredAt,
        lastSequenceNumber: args.sequenceNumber,
        updatedAt
      });
      return existing._id;
    }

    return await ctx.db.insert("devices", {
      deviceId: args.deviceId,
      lastSeenAt: args.measuredAt,
      lastSequenceNumber: args.sequenceNumber,
      updatedAt
    });
  }
});
