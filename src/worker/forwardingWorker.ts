import crypto from "node:crypto";
import { config } from "../config.js";
import { logger } from "../logger.js";
import { runForwardingCycle } from "./queueProcessor.js";

const workerId = `worker-${process.pid}-${crypto.randomUUID()}`;

async function main(): Promise<void> {
  logger.info({ workerId }, "Forwarding worker started");
  await runForwardingCycle(workerId);
  setInterval(() => {
    void runForwardingCycle(workerId).catch((error) => {
      logger.error({ error }, "Forwarding cycle failed");
    });
  }, config.forwarderIntervalMs);
}

void main().catch((error) => {
  logger.fatal({ error }, "Worker terminated unexpectedly");
  process.exit(1);
});
