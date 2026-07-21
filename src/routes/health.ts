import type { FastifyInstance } from "fastify";
import { sendStructured } from "../http/responses.js";

export async function healthRoutes(fastify: FastifyInstance): Promise<void> {
  fastify.get("/healthz", async (_request, reply) => {
    return sendStructured(reply, 200, "ok", "healthy", "Direct ingestion service is healthy.", false, {
      checks: {
        deviceRegistry: "configured",
        convex: "configured"
      }
    });
  });
}
