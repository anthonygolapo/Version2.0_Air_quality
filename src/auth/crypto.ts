import crypto from "node:crypto";

export function sha256Hex(input: string | Buffer): string {
  return crypto.createHash("sha256").update(input).digest("hex");
}

export function hmacSha256Hex(secret: string, message: string): string {
  return crypto.createHmac("sha256", secret).update(message).digest("hex");
}

export function timingSafeEqualHex(expectedHex: string, actualHex: string): boolean {
  try {
    const expected = Buffer.from(expectedHex, "hex");
    const actual = Buffer.from(actualHex, "hex");
    if (expected.length !== actual.length) {
      return false;
    }
    return crypto.timingSafeEqual(expected, actual);
  } catch {
    return false;
  }
}
