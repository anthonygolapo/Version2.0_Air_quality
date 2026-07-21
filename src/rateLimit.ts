interface RateLimitEntry {
  count: number;
  resetAt: number;
}

const counters = new Map<string, RateLimitEntry>();

export function checkRateLimit(key: string, maximum: number, windowMs: number): boolean {
  const now = Date.now();
  const current = counters.get(key);
  if (!current || current.resetAt <= now) {
    counters.set(key, { count: 1, resetAt: now + windowMs });
    return true;
  }

  if (current.count >= maximum) {
    return false;
  }

  current.count += 1;
  if (counters.size > 10_000) {
    for (const [counterKey, entry] of counters) {
      if (entry.resetAt <= now) counters.delete(counterKey);
    }
  }
  return true;
}
