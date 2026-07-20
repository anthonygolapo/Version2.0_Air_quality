import { config } from "../config.js";
import { logger } from "../logger.js";
import { pool } from "../db/postgres.js";
import { runMigrations } from "../db/migrations.js";
import { QueueRepository } from "../db/queueRepository.js";
import { sendBatchToConvex, ConvexRequestError } from "../services/convexClient.js";
import { computeBackoffMs } from "./backoff.js";
import type { QueueRecord } from "../types.js";

const queue = new QueueRepository(pool);

async function handleResult(records: QueueRecord[], result: Awaited<ReturnType<typeof sendBatchToConvex>>) {
  const accepted = new Set<string>();
  const bySequence = new Map(records.map((record) => [record.sequenceNumber, record]));

  for (const sequence of result.acceptedSequenceNumbers) {
    const record = bySequence.get(sequence);
    if (record) {
      accepted.add(record.id);
    }
  }

  for (const sequence of result.duplicateSequenceNumbers) {
    const record = bySequence.get(sequence);
    if (record) {
      accepted.add(record.id);
    }
  }

  await queue.markSent([...accepted]);

  for (const sequence of result.conflictingSequenceNumbers) {
    const record = bySequence.get(sequence);
    if (record) {
      const deadLetter = record.retryCount + 1 >= config.forwarderMaxConflictsBeforeDeadLetter;
      await queue.markFailed(record.id, new Date(Date.now() + computeBackoffMs(record.retryCount)).toISOString(), "convex_conflict", deadLetter);
    }
  }

  for (const rejection of result.rejected) {
    const record = bySequence.get(rejection.sequenceNumber);
    if (!record) {
      continue;
    }
    const deadLetter = !rejection.retryable && record.retryCount + 1 >= config.forwarderMaxConflictsBeforeDeadLetter;
    await queue.markFailed(
      record.id,
      new Date(Date.now() + computeBackoffMs(record.retryCount)).toISOString(),
      rejection.reason,
      deadLetter
    );
  }
}

function batchIdentifier(workerId: string, records: QueueRecord[]): string {
  const first = records[0];
  return `${workerId}-${first.deviceId}-${first.sequenceNumber}-${Date.now()}`;
}

export async function runForwardingCycle(workerId: string) {
  await runMigrations(pool);

  const recovered = await queue.resetStaleProcessingRows(
    new Date(Date.now() - config.forwarderStaleProcessingMs).toISOString()
  );

  const records = await queue.claimBatch(config.forwarderBatchSize, workerId);
  if (records.length === 0) {
    logger.debug({ workerId }, "No records ready for forwarding");
    return {
      recovered,
      claimed: 0,
      forwarded: 0
    };
  }

  try {
    const result = await sendBatchToConvex(records, batchIdentifier(workerId, records));
    await handleResult(records, result);
    logger.info({ workerId, count: records.length, result }, "Convex batch completed");
    return {
      recovered,
      claimed: records.length,
      forwarded: result.acceptedCount + result.duplicateCount,
      result
    };
  } catch (error) {
    const transient = error instanceof ConvexRequestError ? error.transient : true;
    const retryAfter = error instanceof ConvexRequestError && error.retryAfterMs ? error.retryAfterMs : undefined;
    const retryMs = retryAfter ?? computeBackoffMs(Math.max(...records.map((record) => record.retryCount), 0));
    const retryAt = new Date(Date.now() + retryMs).toISOString();
    const message = error instanceof Error ? error.message : "convex_unknown_error";

    if (transient) {
      await queue.markBatchTransientFailure(records.map((record) => record.id), retryAt, message);
      logger.warn({ workerId, retryAt, error: message }, "Convex transient failure; batch retained");
    } else {
      for (const record of records) {
        await queue.markFailed(record.id, retryAt, message);
      }
      logger.error({ workerId, retryAt, error: message }, "Convex permanent failure");
    }

    return {
      recovered,
      claimed: records.length,
      forwarded: 0,
      error: message,
      retryAt
    };
  }
}
