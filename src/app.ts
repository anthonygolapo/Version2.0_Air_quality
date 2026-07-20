import Fastify from "fastify";
import helmet from "@fastify/helmet";
import type { Pool } from "pg";
import { config } from "./config.js";
import { logger } from "./logger.js";
import { runMigrations } from "./db/migrations.js";
import { DeviceRepository } from "./db/deviceRepository.js";
import { QueueRepository } from "./db/queueRepository.js";
import { RateLimitRepository } from "./db/rateLimitRepository.js";
import { sendStructured } from "./http/responses.js";
import { healthRoutes } from "./routes/health.js";
import { readingsRoutes } from "./routes/readings.js";

declare module "fastify" {
  interface FastifyInstance {
    pg: Pool;
    devices: DeviceRepository;
    queue: QueueRepository;
    rateLimits: RateLimitRepository;
  }
}

export async function buildApp(pool: Pool) {
  await runMigrations(pool);

  const app = Fastify({
    loggerInstance: logger,
    bodyLimit: config.requestBodyLimitBytes,
    trustProxy: true
  });

  app.addContentTypeParser("application/json", { parseAs: "string" }, (_request, body, done) => {
    done(null, body);
  });

  app.decorate("pg", pool);
  app.decorate("devices", new DeviceRepository(pool));
  app.decorate("queue", new QueueRepository(pool));
  app.decorate("rateLimits", new RateLimitRepository(pool));

  await app.register(helmet);
  await app.register(healthRoutes);
  await app.register(readingsRoutes);

  app.setErrorHandler((error, request, reply) => {
    if ((error as { code?: string }).code === "FST_ERR_CTP_BODY_TOO_LARGE") {
      return void sendStructured(reply, 413, "error", "payload_too_large", "Request body exceeds the allowed size.", false);
    }

    request.log.error({ err: error }, "Unhandled request error");
    void sendStructured(reply, 500, "error", "internal_error", "Request handling failed.", true);
  });

  return app;
}
