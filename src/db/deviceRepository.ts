import type { Pool } from "pg";
import type { DeviceRecord } from "../types.js";

export class DeviceRepository {
  constructor(private readonly pool: Pool) {}

  async findByDeviceId(deviceId: string): Promise<DeviceRecord | null> {
    const result = await this.pool.query(`
      SELECT
        device_id,
        status,
        credential_version,
        secret_ciphertext,
        previous_secret_ciphertext,
        previous_credential_version
      FROM devices
      WHERE device_id = $1
    `, [deviceId]);

    if (result.rowCount !== 1) {
      return null;
    }

    const row = result.rows[0];
    return {
      deviceId: row.device_id,
      status: row.status,
      credentialVersion: row.credential_version,
      secretCiphertext: row.secret_ciphertext,
      previousSecretCiphertext: row.previous_secret_ciphertext,
      previousCredentialVersion: row.previous_credential_version
    };
  }
}
