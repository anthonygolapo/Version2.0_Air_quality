# Vercel Air-Quality Ingestion System

## Architecture

```text
ESP32-S3
  -> HTTPS POST every 60 seconds
  -> HMAC-SHA256 signed request
  -> durable SPIFFS queue for unsent readings

Vercel Functions
  -> public HTTPS endpoint at /api/v1/readings
  -> Fastify request validation and HMAC verification
  -> persistent PostgreSQL queue commit before 202 response

Managed PostgreSQL
  -> durable cross-instance queue
  -> unique (device_id, sequence_number)
  -> SKIP LOCKED claiming for safe concurrent forwarding

Vercel Cron
  -> GET /api/internal/forward every 10 minutes
  -> batch upload pending readings to Convex

Convex HTTP Action
  -> validates signed batch
  -> de-duplicates and stores final readings
```

## Risks

- Vercel functions have no durable local disk, so queue state stays in PostgreSQL only.
- Vercel cron is function-triggered, so the forwarding step must finish within function limits.
- Function cold starts are acceptable because ingestion durability is in PostgreSQL and the Fastify app is reused at module scope.
- Horizontal scaling is handled in PostgreSQL with row locking and stale-processing recovery.

## Folder Structure

```text
.
├── api/
│   ├── [...path].ts
│   ├── healthz.ts
│   └── internal/
│       └── forward.ts
├── convex/
├── examples/
├── firmware/
├── src/
│   ├── app.ts
│   ├── config.ts
│   ├── logger.ts
│   ├── server.ts
│   ├── auth/
│   ├── db/
│   ├── http/
│   ├── routes/
│   ├── scripts/
│   ├── services/
│   ├── validation/
│   └── worker/
│       ├── backoff.ts
│       ├── forwardingWorker.ts
│       └── queueProcessor.ts
├── .env.example
├── package.json
├── tsconfig.json
└── vercel.json
```

## Endpoints

- `POST /api/v1/readings`: ESP32 ingestion endpoint
- `GET /api/healthz`: Vercel health endpoint
- `GET /api/internal/forward`: cron-triggered forwarding endpoint
- `POST /api/v1/readings/batch`: Convex-side batch endpoint

## ESP32 Integration Points

Only these remain hardware-specific:

- `sensor_reader_collect()`
- `sensor_reader_get_signal_strength()`
- `sensor_reader_get_battery_voltage()`
- `sensor_reader_get_solar_voltage()`
- `sensor_reader_compute_alarm_flags()`

## Vercel Deployment Notes

- Vercel provides the public HTTPS URL and TLS certificates automatically.
- The ingestion API runs through [api/[...path].ts](/C:/Users/ENVI-COMM/Desktop/http_server/api/[...path].ts:1).
- The cron forwarder runs through [api/internal/forward.ts](/C:/Users/ENVI-COMM/Desktop/http_server/api/internal/forward.ts:1).
- Protect the cron endpoint with `CRON_SECRET`.
- Keep queue data in managed PostgreSQL, not local files.
- Set env vars in Vercel for `DATABASE_URL`, HMAC secrets, encryption key, and `CRON_SECRET`.

## Example URLs

- API URL for firmware:
  `https://your-project.vercel.app/api/v1/readings`
- Health check:
  `https://your-project.vercel.app/api/healthz`

## Sample ESP32 Request

Headers:

- `x-device-id`
- `x-sequence-number`
- `x-timestamp`
- `x-credential-version`
- `x-signature`

Body example: [examples/reading.json](/C:/Users/ENVI-COMM/Desktop/http_server/examples/reading.json:1)

## Sample Success Response

```json
{
  "status": "accepted",
  "code": "queued_successfully",
  "message": "Reading was committed to persistent queue storage.",
  "retryable": false,
  "serverTime": "2026-07-17T10:00:00.000Z",
  "deviceId": "AQ01",
  "sequenceNumber": 1842,
  "queuedAt": "2026-07-17T10:00:00.000Z"
}
```

## Curl Test

```powershell
curl.exe https://your-project.vercel.app/api/v1/readings `
  -H "content-type: application/json" `
  -H "x-device-id: AQ01" `
  -H "x-sequence-number: 1842" `
  -H "x-timestamp: 2026-07-17T10:00:00Z" `
  -H "x-credential-version: 1" `
  -H "x-signature: <hex-signature>" `
  --data @examples/reading.json
```

Manual forward test:

```powershell
curl.exe "https://your-project.vercel.app/api/internal/forward?secret=<CRON_SECRET>"
```

## Setup Steps

1. Provision managed PostgreSQL.
2. Add Vercel env vars from [.env.example](/C:/Users/ENVI-COMM/Desktop/http_server/.env.example:1).
3. Deploy this repo to Vercel.
4. Register a device with `npm run register-device`.
5. Deploy Convex and set the matching Convex env vars.
6. Put `https://your-project.vercel.app/api/v1/readings` into [firmware/main/config.h](/C:/Users/ENVI-COMM/Desktop/http_server/firmware/main/config.h:1) as `NODE_SERVER_URL`.
7. Flash the ESP32 firmware.

## Troubleshooting

- `401`: wrong device secret, bad timestamp, or HMAC mismatch.
- `403`: device disabled in PostgreSQL.
- `409`: reused sequence number with a different payload.
- `429`: rate-limited device or IP.
- `503`: PostgreSQL or Convex temporarily unavailable.
- Forwarding stuck: verify Vercel cron is active and `CRON_SECRET` is set.
