import { config } from "../config.js";
import { encryptSecret } from "../auth/crypto.js";
import { pool } from "../db/postgres.js";
import { runMigrations } from "../db/migrations.js";

function getArg(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

const deviceId = getArg("--deviceId");
const secret = getArg("--secret");
const credentialVersion = Number(getArg("--credentialVersion") ?? "1");

if (!deviceId || !secret || !Number.isInteger(credentialVersion)) {
  console.error("Usage: npm run register-device -- --deviceId AQ01 --secret <secret> --credentialVersion 1");
  process.exit(1);
}

await runMigrations(pool);
const ciphertext = encryptSecret(secret, config.deviceSecretEncryptionKey);

await pool.query(`
  INSERT INTO devices(device_id, status, credential_version, secret_ciphertext, created_at, updated_at)
  VALUES ($1, 'active', $2, $3, NOW(), NOW())
  ON CONFLICT (device_id)
  DO UPDATE SET
    previous_credential_version = CASE
      WHEN devices.credential_version <> EXCLUDED.credential_version THEN devices.credential_version
      ELSE devices.previous_credential_version
    END,
    previous_secret_ciphertext = CASE
      WHEN devices.credential_version <> EXCLUDED.credential_version THEN devices.secret_ciphertext
      ELSE devices.previous_secret_ciphertext
    END,
    credential_version = EXCLUDED.credential_version,
    secret_ciphertext = EXCLUDED.secret_ciphertext,
    status = 'active',
    updated_at = NOW()
`, [deviceId, credentialVersion, ciphertext]);

console.log(`Registered device ${deviceId} at credential version ${credentialVersion}.`);
await pool.end();
