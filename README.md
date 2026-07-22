# Direct ESP32 Air-Quality Ingestion

## Architecture

```text
Sensors --one reading/minute--> ESP32 SPIFFS queue
ESP32 --signed HTTPS batch of 10--> Vercel API
Vercel --validated server-signed batch--> Convex HTTP Action
Convex --accepted/duplicate/rejected sequences--> Vercel --> ESP32
```

The ESP32 is the temporary persistent queue. Supabase, PostgreSQL, the forwarding worker, and the GitHub scheduler are not part of this architecture.

## Reliability Contract

1. The ESP32 writes every reading to SPIFFS before network transmission.
2. A normal upload starts when at least 10 queued readings exist.
3. Vercel authenticates the device HMAC and validates every field.
4. Vercel immediately forwards valid readings to Convex over HTTPS.
5. Vercel returns success only after Convex returns per-sequence results.
6. The ESP32 deletes accepted and duplicate sequences only.
7. Conflicting and permanently rejected records are renamed as local dead-letter files.
8. Network and temporary server failures leave the batch in SPIFFS for retry.

Convex prevents duplicate final records with `deviceId + sequenceNumber`.

## Endpoints

- `POST /api/v1/readings`: signed ESP32 batch ingestion and direct forwarding
- `GET /api/healthz`: stateless Vercel service health
- `POST /api/v1/readings/batch`: authenticated Convex HTTP Action

## Device Request

Headers:

- `x-device-id`
- `x-batch-id`
- `x-timestamp`
- `x-credential-version`
- `x-signature`

The HMAC-SHA256 canonical request is:

```text
POST
/api/v1/readings
<deviceId>
<batchId>
<timestamp>
<credentialVersion>
<sha256-lowercase-hex-of-raw-body>
```

See `examples/device-batch.json` for the request body. Production firmware sends 10 readings; the API accepts 1-10 so recovery and diagnostics can use smaller batches.

## Success Response

```json
{
  "status": "ok",
  "code": "batch_stored",
  "message": "Convex processed the reading batch.",
  "retryable": false,
  "deviceId": "AQ01",
  "batchId": "AQ01-0000001842-0000001851",
  "acceptedSequenceNumbers": [1842],
  "duplicateSequenceNumbers": [],
  "conflictingSequenceNumbers": [],
  "rejectedSequenceNumbers": [],
  "retryableSequenceNumbers": []
}
```

The ESP32 treats HTTP `200` plus valid sequence arrays as a completed attempt. HTTP `429` or `5xx`, TLS errors, malformed responses, and missing sequence confirmations retain local files.

DGS2 gas fields (`co`, `no2`, `o3`, and `so2`) are transmitted and stored in ppb. The configured models are 97x-100 CO, 97x-500 NO2, 97x-400 O3, and 97x-600 SO2.

## Vercel Environment

Configure the variables in `.env.example`. `DEVICE_CREDENTIALS_JSON` is sensitive and must contain the same unique secret and credential version as each device firmware:

```json
[
  {
    "deviceId": "AQ01",
    "status": "active",
    "credentialVersion": 1,
    "secret": "a-unique-secret-for-this-device"
  }
]
```

Do not commit real credentials. Remove the old `DATABASE_URL`, `DEVICE_SECRET_ENCRYPTION_KEY`, `FORWARDER_*`, and `CRON_SECRET` variables after direct ingestion is verified.

Copy `firmware/main/config.example.h` to `firmware/main/config.h` for local firmware builds. `config.h` is intentionally ignored because it contains Wi-Fi and device secrets.

## Convex Deployment

Convex requires `CONVEX_HMAC_SECRET`, `CONVEX_CREDENTIAL_VERSION`, and `CONVEX_MAX_SKEW_MS`. The HMAC secret and version must match Vercel.

```powershell
npx convex deploy --yes
```

## Build And Test

```powershell
npm install
npm test
npm run build
```

Firmware from an ESP-IDF Installation Manager PowerShell:

```powershell
cd C:\Users\ENVI-COMM\Desktop\http_server\firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Do not flash until Vercel has `DEVICE_CREDENTIALS_JSON` configured and the direct API deployment is healthy.

## Hardware Integration Points

The architecture does not change sensor drivers. Hardware-specific work remains in `sensor_reader.c`, including particulate, gas, temperature/humidity, battery, solar, RSSI, and alarm collection.

## Troubleshooting

- `400`: headers, batch ID, device ID, or JSON structure do not agree.
- `401`: device secret/version, timestamp, or signature is wrong.
- `403`: the device is disabled in `DEVICE_CREDENTIALS_JSON`.
- `413`: request body or reading count exceeds configured limits.
- `422`: one or more sensor fields are malformed or implausible.
- `429`: request rate exceeded; retain and retry.
- `502` or `503`: Convex did not confirm storage; retain and retry.
- Queue remains below 10: expected; the next upload occurs when the tenth reading is persisted.
