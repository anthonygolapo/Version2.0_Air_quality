import type { IncomingMessage, ServerResponse } from "node:http";

export default async function handler(_req: IncomingMessage, res: ServerResponse) {
  res.statusCode = 200;
  res.setHeader("content-type", "application/json");
  res.end(JSON.stringify({
    status: "ok",
    code: "healthy",
    message: "Direct ingestion service is healthy.",
    retryable: false,
    serverTime: new Date().toISOString(),
    checks: {
      deviceRegistry: "configured",
      convex: "configured"
    }
  }));
}
