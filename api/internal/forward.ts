import type { IncomingMessage, ServerResponse } from "node:http";
import { config } from "../../src/config.js";
import { runForwardingCycle } from "../../src/worker/queueProcessor.js";

function getQuerySecret(url?: string): string | null {
  if (!url) {
    return null;
  }

  const parsed = new URL(url, "https://placeholder.local");
  return parsed.searchParams.get("secret");
}

export default async function handler(req: IncomingMessage, res: ServerResponse) {
  const authHeader = req.headers.authorization;
  const cronSecret = process.env.CRON_SECRET;
  const querySecret = getQuerySecret(req.url);

  if (cronSecret) {
    const bearerMatch = authHeader === `Bearer ${cronSecret}`;
    const queryMatch = querySecret === cronSecret;
    if (!bearerMatch && !queryMatch) {
      res.statusCode = 401;
      res.setHeader("content-type", "application/json");
      res.end(JSON.stringify({
        status: "error",
        code: "authentication_failed",
        message: "Cron authentication failed.",
        retryable: false,
        serverTime: new Date().toISOString()
      }));
      return;
    }
  }

  if (req.method !== "GET") {
    res.statusCode = 405;
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify({
      status: "error",
      code: "method_not_allowed",
      message: "Only GET is allowed for this endpoint.",
      retryable: false,
      serverTime: new Date().toISOString()
    }));
    return;
  }

  const result = await runForwardingCycle(`vercel-cron-${Date.now()}`);
  res.statusCode = 200;
  res.setHeader("content-type", "application/json");
  res.end(JSON.stringify({
    status: "ok",
    code: "forwarding_cycle_completed",
    message: "Forwarding cycle completed.",
    retryable: false,
    serverTime: new Date().toISOString(),
    ...result,
    configuredIntervalMs: config.forwarderIntervalMs
  }));
}
