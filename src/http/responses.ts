import type { FastifyReply } from "fastify";

export function sendStructured(
  reply: FastifyReply,
  statusCode: number,
  status: string,
  code: string,
  message: string,
  retryable: boolean,
  extra: Record<string, unknown> = {}
) {
  return reply.code(statusCode).send({
    status,
    code,
    message,
    retryable,
    serverTime: new Date().toISOString(),
    ...extra
  });
}
