import { Pool } from "pg";
import { config } from "../config.js";
import { logger } from "../logger.js";

export const pool = new Pool({
  connectionString: config.databaseUrl,
  max: 10,
  idleTimeoutMillis: 30000
});

pool.on("error", (error) => {
  logger.error({ error }, "Unexpected PostgreSQL pool error");
});
