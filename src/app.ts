import Fastify from "fastify";
import helmet from "@fastify/helmet";
import { config } from "./config.js";
import { logger } from "./logger.js";
import { sendStructured } from "./http/responses.js";
import { healthRoutes } from "./routes/health.js";
import { readingsRoutes } from "./routes/readings.js";

export async function buildApp() {
  const app = Fastify({
    loggerInstance: logger,
    bodyLimit: config.requestBodyLimitBytes,
    trustProxy: true
  });

  app.addContentTypeParser("application/json", { parseAs: "string" }, (_request, body, done) => {
    done(null, body);
  });

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
