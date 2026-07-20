import { httpRouter } from "convex/server";
import { httpAction } from "./_generated/server";
import { api } from "./_generated/api";
import { isFreshTimestamp, sha256Hex, verifyConvexSignature } from "./auth";

const http = httpRouter();

http.route({
  path: "/api/v1/readings/batch",
  method: "POST",
  handler: httpAction(async (ctx, request) => {
    const batchId = request.headers.get("x-batch-id") ?? "";
    const timestamp = request.headers.get("x-timestamp") ?? "";
    const credentialVersion = request.headers.get("x-credential-version") ?? "";
    const signature = request.headers.get("x-signature") ?? "";

    if (!batchId || !timestamp || !credentialVersion || !signature) {
      return new Response(JSON.stringify({
        status: "error",
        code: "invalid_request",
        message: "Missing required authentication headers.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 400, headers: { "content-type": "application/json" } });
    }

    const maxSkewMs = Number(process.env.CONVEX_MAX_SKEW_MS ?? "300000");
    if (!isFreshTimestamp(timestamp, maxSkewMs)) {
      return new Response(JSON.stringify({
        status: "error",
        code: "authentication_failed",
        message: "Batch timestamp is expired or invalid.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 401, headers: { "content-type": "application/json" } });
    }

    const expectedVersion = String(process.env.CONVEX_CREDENTIAL_VERSION ?? "1");
    if (credentialVersion !== expectedVersion) {
      return new Response(JSON.stringify({
        status: "error",
        code: "authentication_failed",
        message: "Credential version is invalid.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 401, headers: { "content-type": "application/json" } });
    }

    const rawBody = await request.text();
    if (!verifyConvexSignature(rawBody, { batchId, timestamp, credentialVersion, signature })) {
      return new Response(JSON.stringify({
        status: "error",
        code: "authentication_failed",
        message: "Batch signature verification failed.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 401, headers: { "content-type": "application/json" } });
    }

    let payload: unknown;
    try {
      payload = JSON.parse(rawBody);
    } catch {
      return new Response(JSON.stringify({
        status: "error",
        code: "invalid_request",
        message: "Batch body must be valid JSON.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 400, headers: { "content-type": "application/json" } });
    }

    const records = (payload as { records?: unknown[] }).records;
    if (!Array.isArray(records) || records.length === 0 || records.length > 1000) {
      return new Response(JSON.stringify({
        status: "error",
        code: "invalid_request",
        message: "Batch must include 1 to 1000 records.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 400, headers: { "content-type": "application/json" } });
    }

    const result = await ctx.runMutation(api.readings.ingestBatch, {
      batchId,
      requestHash: sha256Hex(rawBody),
      timestamp,
      records: records as never
    });

    if (result.replay) {
      return new Response(JSON.stringify({
        status: "error",
        code: "replayed_batch",
        message: "Batch was already processed.",
        retryable: false,
        serverTime: new Date().toISOString()
      }), { status: 409, headers: { "content-type": "application/json" } });
    }

    return new Response(JSON.stringify(result), {
      status: 200,
      headers: { "content-type": "application/json" }
    });
  })
});

export default http;
