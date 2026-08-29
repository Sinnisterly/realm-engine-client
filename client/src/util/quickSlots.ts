/**
 * Typed helpers for player quickslot (potion belt) state.
 */

import type { ClientConnection } from '../proxy/ClientConnection.js';

export interface QuickSlotState {
  itemType: number;
  quantity: number;
}

export function quickSlotCount(client: ClientConnection): number {
  return client.playerData.hasThirdQuickSlot ? 3 : 2;
}

export function readQuickSlot(client: ClientConnection, slot: number): QuickSlotState {
  const raw = client.playerData.quickSlots?.[slot];

  if (typeof raw === 'number') {
    return { itemType: raw > 0 ? raw : -1, quantity: 0 };
  }

  if (raw && typeof raw === 'object') {
    const itemTypeRaw = Number(raw.itemType ?? -1);
    const quantityRaw = Number(raw.quantity ?? 0);
    return {
      itemType: itemTypeRaw > 0 ? itemTypeRaw : -1,
      quantity: Number.isFinite(quantityRaw) ? Math.max(0, Math.trunc(quantityRaw)) : 0,
    };
  }

  return { itemType: -1, quantity: 0 };
}
