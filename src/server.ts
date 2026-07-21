import { buildApp } from "./app.js";
import { config } from "./config.js";
import { logger } from "./logger.js";

async function main(): Promise<void> {
  const app = await buildApp();

  const shutdown = async (signal: string) => {
    logger.info({ signal }, "Shutdown signal received");
    await app.close();
    process.exit(0);
  };

  process.on("SIGINT", () => void shutdown("SIGINT"));
  process.on("SIGTERM", () => void shutdown("SIGTERM"));

  await app.listen({
    host: config.host,
    port: config.port
  });

  logger.info({ host: config.host, port: config.port }, "Managed-hosting API listening");
}

void main().catch((error) => {
  logger.fatal({ error }, "Server failed to start");
  process.exit(1);
});
