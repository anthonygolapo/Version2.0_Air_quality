import pino from "pino";

export const logger = pino({
  level: process.env.NODE_ENV === "development" ? "debug" : "info",
  redact: {
    paths: [
      "req.headers.x-signature",
      "headers.x-signature",
      "signature",
      "secret",
      "secretCiphertext",
      "authorization"
    ],
    censor: "[REDACTED]"
  }
});
