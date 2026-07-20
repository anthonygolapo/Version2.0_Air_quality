import type { IncomingMessage, ServerResponse } from "node:http";
import { pool } from "../src/db/postgres.js";

export default async function handler(_req: IncomingMessage, res: ServerResponse) {
  try {
    await pool.query("SELECT 1");
    res.statusCode = 200;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({
      status: "ok",
      code: "healthy",
      message: "Service is healthy.",
      retryable: false,
      serverTime: new Date().toISOString(),
      checks: {
        database: "ok"
      }
    }));
  } catch {
    res.statusCode = 503;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({
      status: "error",
      code: "database_unavailable",
      message: "Database health check failed.",
      retryable: true,
      serverTime: new Date().toISOString(),
      checks: {
        database: "failed"
      }
    }));
  }
}
