import type { Pool } from "pg";

export class RateLimitRepository {
  constructor(private readonly pool: Pool) {}

  async checkAndIncrement(scope: string, key: string, max: number, windowMs: number): Promise<boolean> {
    const now = Date.now();
    const windowStart = new Date(now - (now % windowMs)).toISOString();

    const result = await this.pool.query(`
      INSERT INTO rate_limit_buckets(scope, key, window_start, hit_count)
      VALUES ($1, $2, $3::timestamptz, 1)
      ON CONFLICT (scope, key)
      DO UPDATE SET
        window_start = CASE
          WHEN rate_limit_buckets.window_start = EXCLUDED.window_start THEN rate_limit_buckets.window_start
          ELSE EXCLUDED.window_start
        END,
        hit_count = CASE
          WHEN rate_limit_buckets.window_start = EXCLUDED.window_start THEN rate_limit_buckets.hit_count + 1
          ELSE 1
        END
      RETURNING hit_count, window_start
    `, [scope, key, windowStart]);

    return result.rows[0].hit_count <= max;
  }
}
