/**
 * Per-connection rate limits on plugin-generated (C->S) packets.
 * Player passthrough traffic is not counted here; only sendToServer() paths.
 */

import type { Packet } from '../packets/Packet.js';
import { PacketBudget } from '../util/packetBudget.js';
import { Logger } from '../util/Logger.js';

/** Max synthetic packets per second by packet name. */
const PER_TYPE_LIMITS: Record<string, number> = {
  USEITEM: 12,
  INVENTORYSWAP: 6,
  ESCAPE: 4,
  INVDROP: 8,
  GOTOACK: 6,
  TELEPORT: 4,
};

/** Hard ceiling on all synthetic outbound packets combined. */
const TOTAL_SYNTHETIC_PER_SEC = 24;

export class OutboundPacketGuard {
  private readonly perType = new Map<string, PacketBudget>();
  private readonly total = new PacketBudget(TOTAL_SYNTHETIC_PER_SEC);
  private lastLogAt = 0;

  allow(packet: Packet): boolean {
    const name = packet.name;
    if (!name) return true;

    const typeBudget = this.budgetFor(name);
    const now = Date.now();

    if (!this.total.tryConsume(now)) {
      this.logDrop(name, 'total budget');
      return false;
    }
    if (!typeBudget.tryConsume(now)) {
      this.logDrop(name, 'per-type budget');
      return false;
    }
    return true;
  }

  private budgetFor(name: string): PacketBudget {
    let budget = this.perType.get(name);
    if (!budget) {
      const limit = PER_TYPE_LIMITS[name] ?? 10;
      budget = new PacketBudget(limit);
      this.perType.set(name, budget);
    }
    return budget;
  }

  private logDrop(packetName: string, reason: string): void {
    const now = Date.now();
    if (now - this.lastLogAt < 2000) return;
    this.lastLogAt = now;
    Logger.warn('OutboundGuard', `Dropped synthetic ${packetName} (${reason})`);
  }
}
