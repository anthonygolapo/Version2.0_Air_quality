import type { Pool, PoolClient } from "pg";
import type { AirQualityReading, ConvexBatchResponse, QueueRecord } from "../types.js";

export class DuplicateReadingError extends Error {
  constructor(public readonly conflicting: boolean) {
    super(conflicting ? "Sequence number already exists with conflicting payload." : "Reading already queued.");
  }
}

export class QueueRepository {
  constructor(private readonly pool: Pool) {}

  async enqueue(reading: AirQualityReading, payloadHash: string, rawBody: string, receivedAt: string): Promise<void> {
    try {
      await this.pool.query(`
        INSERT INTO readings_queue (
          device_id, sequence_number, measured_at, received_at, payload_hash, payload_json,
          pm1, pm25, pm10, co, no2, o3, so2, temperature_c, humidity_percent,
          battery_voltage, solar_voltage, signal_strength, firmware_version, alarm_flags,
          status, retry_count, next_retry_at, created_at
        ) VALUES (
          $1, $2, $3::timestamptz, $4::timestamptz, $5, $6::jsonb,
          $7, $8, $9, $10, $11, $12, $13, $14, $15,
          $16, $17, $18, $19, $20,
          'pending', 0, $4::timestamptz, $4::timestamptz
        )
      `, [
        reading.deviceId,
        reading.sequenceNumber,
        reading.measuredAt,
        receivedAt,
        payloadHash,
        rawBody,
        reading.pm1,
        reading.pm25,
        reading.pm10,
        reading.co,
        reading.no2,
        reading.o3,
        reading.so2,
        reading.temperatureC,
        reading.humidityPercent,
        reading.batteryVoltage,
        reading.solarVoltage,
        reading.signalStrength,
        reading.firmwareVersion,
        reading.alarmFlags
      ]);
    } catch (error) {
      const existing = await this.pool.query(`
        SELECT payload_hash FROM readings_queue WHERE device_id = $1 AND sequence_number = $2
      `, [reading.deviceId, reading.sequenceNumber]);

      if (existing.rowCount === 1) {
        throw new DuplicateReadingError(existing.rows[0].payload_hash !== payloadHash);
      }
      throw error;
    }
  }

  async resetStaleProcessingRows(staleBefore: string): Promise<number> {
    const result = await this.pool.query(`
      UPDATE readings_queue
      SET status = 'pending',
          claimed_by = NULL,
          claimed_at = NULL,
          next_retry_at = NOW(),
          last_error = COALESCE(last_error, 'stale_processing_recovered')
      WHERE status = 'processing' AND claimed_at < $1::timestamptz
    `, [staleBefore]);
    return result.rowCount ?? 0;
  }

  async claimBatch(limit: number, workerId: string): Promise<QueueRecord[]> {
    const client = await this.pool.connect();
    try {
      await client.query("BEGIN");
      const result = await client.query(`
        WITH candidates AS (
          SELECT id
          FROM readings_queue
          WHERE status IN ('pending', 'failed')
            AND (next_retry_at IS NULL OR next_retry_at <= NOW())
          ORDER BY id ASC
          FOR UPDATE SKIP LOCKED
          LIMIT $1
        )
        UPDATE readings_queue rq
        SET status = 'processing',
            claimed_by = $2,
            claimed_at = NOW()
        FROM candidates
        WHERE rq.id = candidates.id
        RETURNING rq.*
      `, [limit, workerId]);
      await client.query("COMMIT");
      return result.rows.map(mapQueueRecord);
    } catch (error) {
      await client.query("ROLLBACK");
      throw error;
    } finally {
      client.release();
    }
  }

  async markSent(ids: string[]): Promise<void> {
    if (ids.length === 0) {
      return;
    }
    await this.pool.query(`
      UPDATE readings_queue
      SET status = 'sent',
          sent_at = NOW(),
          last_error = NULL,
          claimed_at = NULL,
          claimed_by = NULL
      WHERE id = ANY($1::bigint[])
    `, [ids]);
  }

  async markFailed(id: string, retryAt: string, errorText: string, deadLetter = false): Promise<void> {
    const client = await this.pool.connect();
    try {
      await client.query("BEGIN");
      await client.query(`
        UPDATE readings_queue
        SET status = 'failed',
            retry_count = retry_count + 1,
            next_retry_at = $2::timestamptz,
            last_error = LEFT($3, 512),
            claimed_at = NULL,
            claimed_by = NULL
        WHERE id = $1::bigint
      `, [id, retryAt, errorText]);

      if (deadLetter) {
        await client.query(`
          INSERT INTO queue_dead_letters(queue_id, device_id, sequence_number, reason)
          SELECT id, device_id, sequence_number, $2
          FROM readings_queue
          WHERE id = $1::bigint
        `, [id, errorText]);
      }
      await client.query("COMMIT");
    } catch (error) {
      await client.query("ROLLBACK");
      throw error;
    } finally {
      client.release();
    }
  }

  async markBatchTransientFailure(ids: string[], retryAt: string, errorText: string): Promise<void> {
    if (ids.length === 0) {
      return;
    }
    await this.pool.query(`
      UPDATE readings_queue
      SET status = 'failed',
          retry_count = retry_count + 1,
          next_retry_at = $2::timestamptz,
          last_error = LEFT($3, 512),
          claimed_at = NULL,
          claimed_by = NULL
      WHERE id = ANY($1::bigint[])
    `, [ids, retryAt, errorText]);
  }
}

function mapQueueRecord(row: Record<string, unknown>): QueueRecord {
  return {
    id: String(row.id),
    deviceId: String(row.device_id),
    sequenceNumber: Number(row.sequence_number),
    measuredAt: new Date(String(row.measured_at)).toISOString(),
    receivedAt: new Date(String(row.received_at)).toISOString(),
    payloadHash: String(row.payload_hash),
    pm1: Number(row.pm1),
    pm25: Number(row.pm25),
    pm10: Number(row.pm10),
    co: Number(row.co),
    no2: Number(row.no2),
    o3: Number(row.o3),
    so2: Number(row.so2),
    temperatureC: Number(row.temperature_c),
    humidityPercent: Number(row.humidity_percent),
    batteryVoltage: Number(row.battery_voltage),
    solarVoltage: Number(row.solar_voltage),
    signalStrength: Number(row.signal_strength),
    firmwareVersion: String(row.firmware_version),
    alarmFlags: Number(row.alarm_flags),
    status: row.status as QueueRecord["status"],
    retryCount: Number(row.retry_count),
    nextRetryAt: row.next_retry_at ? new Date(String(row.next_retry_at)).toISOString() : null,
    lastError: row.last_error ? String(row.last_error) : null,
    claimedBy: row.claimed_by ? String(row.claimed_by) : null,
    claimedAt: row.claimed_at ? new Date(String(row.claimed_at)).toISOString() : null,
    createdAt: new Date(String(row.created_at)).toISOString(),
    sentAt: row.sent_at ? new Date(String(row.sent_at)).toISOString() : null
  };
}
