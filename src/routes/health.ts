import type { FastifyInstance } from "fastify";
import { sendStructured } from "../http/responses.js";

export async function healthRoutes(fastify: FastifyInstance): Promise<void> {
  fastify.get("/healthz", async (_request, reply) => {
    try {
      await fastify.pg.query("SELECT 1");
      return sendStructured(reply, 200, "ok", "healthy", "Service is healthy.", false, {
        checks: {
          database: "ok"
        }
      });
    } catch {
      return sendStructured(reply, 503, "error", "database_unavailable", "Database health check failed.", true, {
        checks: {
          database: "failed"
        }
      });
    }
  });
}
