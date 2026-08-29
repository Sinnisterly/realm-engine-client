/**
 * Sliding-window send budget. Used by plugins and OutboundPacketGuard to cap
 * how many synthetic packets go to the server per second.
 */

export class PacketBudget {
  private readonly timestamps: number[] = [];

  constructor(private readonly maxPerSecond: number) {}

  /** Remaining sends allowed in the current 1s window. */
  remaining(now = Date.now()): number {
    this.prune(now);
    return Math.max(0, this.maxPerSecond - this.timestamps.length);
  }

  /** Record one send. Returns false when the budget is exhausted. */
  tryConsume(now = Date.now()): boolean {
    this.prune(now);
    if (this.timestamps.length >= this.maxPerSecond) return false;
    this.timestamps.push(now);
    return true;
  }

  private prune(now: number): void {
    const cutoff = now - 1000;
    let expired = 0;
    while (expired < this.timestamps.length && this.timestamps[expired] <= cutoff) {
      expired++;
    }
    if (expired > 0) this.timestamps.splice(0, expired);
  }
}
