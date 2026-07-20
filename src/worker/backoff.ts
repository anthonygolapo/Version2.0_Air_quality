export function computeBackoffMs(retryCount: number, baseMs = 5000, capMs = 6 * 60 * 60 * 1000): number {
  const exponent = Math.min(retryCount, 12);
  const delay = Math.min(capMs, baseMs * 2 ** exponent);
  const jitter = Math.floor(Math.random() * Math.max(1, Math.floor(delay * 0.2)));
  return delay + jitter;
}
