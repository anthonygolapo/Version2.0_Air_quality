import type { IncomingMessage, ServerResponse } from "node:http";
import { buildApp } from "../src/app.js";
import { pool } from "../src/db/postgres.js";

const appPromise = buildApp(pool);

export default async function handler(req: IncomingMessage, res: ServerResponse) {
  const app = await appPromise;
  await app.ready();
  app.server.emit("request", req, res);
}
