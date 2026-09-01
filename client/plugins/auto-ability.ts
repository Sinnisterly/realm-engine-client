import type { PluginContext } from '../src/plugins/PluginContext.js';
import type { ClientConnection } from '../src/proxy/ClientConnection.js';

// Class-autodetected auto ability. Fires a USEITEM for the ability slot (the
// same proven mechanism auto-drink uses for potions). Point-aimed classes fire
// at the nearest enemy; everyone else self-casts on the same interval.
const ABILITY_SLOT = 1;
// Self-buffs last a few seconds and have a usage cooldown; re-casting every
// tick spams past that cooldown and eventually crashes the game. Fixed
// intervals avoid it without per-item cooldown bookkeeping.
// Defaults for the configurable fire intervals. These are the minimum gap
// between casts, not the item's real cooldown; there is no per-item cooldown
// bookkeeping, so setting them too low spams the server.
const DEFAULT_SELF_INTERVAL_MS = 2500;
const DEFAULT_TARGET_INTERVAL_MS = 1000;
const MIN_INTERVAL_MS = 250;
const MAX_INTERVAL_MS = 10000;
const MANUAL_PAUSE_MS = 3000;
// Drop targets that haven't updated recently so we don't fire at a ghost
// (a despawned/out-of-view enemy). This was 500 ms, which is shorter than the
// gap between NEWTICKs that carry a given entity: the server only sends a
// status for an object when something about it changed, so an enemy standing
// still and taking no damage goes "stale" while it is right in front of you.
// auto-ability is the only caller of maxStaleMs anywhere in the client, so the
// tight window was never exercised elsewhere. Despawns are handled by the
// world state's own `drops` handling, not by this.
const DEFAULT_TARGET_MAX_STALE_MS = 3000;
/** At most one diagnostic line per second, so a blocked gate cannot flood. */
const DIAG_THROTTLE_MS = 1000;

// Aimed abilities -> fire at nearest enemy.
const TARGET_CLASSES = new Set<number>([
  775, 782, 785, 798, 800, 801, 802, 803, 805, 806, 817,
]);
// Self/area buffs -> fire at own position. Rogue (768) is excluded: its cloak
// is a utility stealth, not something to auto-cast.
const SELF_CLASSES = new Set<number>([784, 796, 797, 799]);
// Trickster/Kensei omitted entirely: their abilities move the player.

const SAFE_ZONE_SUBSTRINGS = ['nexus', 'vault', 'guild hall', 'cloth bazaar', 'daily quest', 'daily login', 'pet yard', 'grand bazaar'];

// Block any ability item that moves the player (prisms, sheaths, Planewalker).
const MOVEMENT_ACTIVATE_RE =
  /<Activate\b[^>]*>\s*(?:Teleport|TeleportToObject|MarkAndTeleport|Dash|ChannelDash)\s*<\/Activate>/;
// The item's real MP cost, so the gate can ask "can this even be cast" instead
// of inferring it from a percentage of the bar.
const MP_COST_RE = /<MpCost>\s*([0-9]+(?:\.[0-9]+)?)\s*<\/MpCost>/;

export function register(ctx: PluginContext) {
  ctx.name = 'Auto Ability';
  ctx.category = 'combat';

  // A reserve, not a floor on the full bar: casting is allowed until this much
  // MP is left. The old `mpFloorPct` defaulted to 85 and only ever passed
  // because the MP ratio was computed against the gearless base and read above
  // 100%. Once that base was corrected the 85 became real, and the plugin went
  // silent after its first cast. New key, so a stored 85 does not carry over.
  let mpReservePct = 25;
  let safeZonePause = true;
  let abilityRange = 12;
  // Off by default. The old `minTargetMaxHp` defaulted to 1000, which quietly
  // limited aimed classes to gods and bosses and blocked self-casters outright.
  let targetMinMaxHp = 0;
  let skipScenery = true;
  let selfNeedsTarget = true;
  let targetMaxStaleMs = DEFAULT_TARGET_MAX_STALE_MS;
  let selfIntervalMs = DEFAULT_SELF_INTERVAL_MS;
  let targetIntervalMs = DEFAULT_TARGET_INTERVAL_MS;
  let diagnostics = false;

  const safeZone = new WeakMap<ClientConnection, boolean>();
  const nextAllowedAt = new WeakMap<ClientConnection, number>();
  const mpCostCache = new Map<number, number>();
  let selfFiring = false;
  let lastDiagAt = 0;

  ctx.registerSetting('mpReservePct', {
    label: 'Keep MP above % (0 = spend it all)',
    type: 'range', value: 25, min: 0, max: 100, step: 5,
  }, (v: number) => { mpReservePct = Math.max(0, Math.min(100, Math.trunc(Number(v) || 0))); });

  ctx.registerSetting('safeZonePause', {
    label: 'Pause in safe zones',
    type: 'boolean', value: true,
  }, (v: boolean) => { safeZonePause = v === true; });

  ctx.registerSetting('abilityRange', {
    label: 'Aimed range (tiles)',
    type: 'range', value: 12, min: 3, max: 30, step: 1,
  }, (v: number) => { abilityRange = Math.max(3, Math.min(30, Math.trunc(Number(v) || 12))); });

  ctx.registerSetting('targetMinMaxHp', {
    label: 'Min target max HP (0 = any enemy)',
    type: 'number', value: 0, min: 0, max: 200000, step: 250,
  }, (v: number) => { targetMinMaxHp = Math.max(0, Math.trunc(Number(v) || 0)); });

  ctx.registerSetting('skipScenery', {
    label: 'Skip walls / breakables',
    type: 'boolean', value: true,
  }, (v: boolean) => { skipScenery = v === true; });

  ctx.registerSetting('selfNeedsTarget', {
    label: 'Self-cast only near an enemy',
    type: 'boolean', value: true,
  }, (v: boolean) => { selfNeedsTarget = v === true; });

  ctx.registerSetting('targetMaxStaleMs', {
    label: 'Target freshness (ms, 0 = ignore)',
    type: 'number', value: DEFAULT_TARGET_MAX_STALE_MS, min: 0, max: 10000, step: 250,
  }, (v: number) => { targetMaxStaleMs = Math.max(0, Math.min(10000, Math.trunc(Number(v) || 0))); });

  const clampInterval = (v: number, fallback: number) =>
    Math.max(MIN_INTERVAL_MS, Math.min(MAX_INTERVAL_MS, Math.trunc(Number(v) || fallback)));

  ctx.registerSetting('targetIntervalMs', {
    label: 'Aimed cooldown (ms)',
    type: 'number', value: DEFAULT_TARGET_INTERVAL_MS,
    min: MIN_INTERVAL_MS, max: MAX_INTERVAL_MS, step: 50,
  }, (v: number) => { targetIntervalMs = clampInterval(v, DEFAULT_TARGET_INTERVAL_MS); });

  ctx.registerSetting('selfIntervalMs', {
    label: 'Self-cast cooldown (ms)',
    type: 'number', value: DEFAULT_SELF_INTERVAL_MS,
    min: MIN_INTERVAL_MS, max: MAX_INTERVAL_MS, step: 50,
  }, (v: number) => { selfIntervalMs = clampInterval(v, DEFAULT_SELF_INTERVAL_MS); });

  ctx.registerSetting('diagnostics', {
    label: 'Diagnostics (log why it is not firing)',
    type: 'boolean', value: false,
  }, (v: boolean) => { diagnostics = v === true; });

  /** Throttled reason log — the point is to make a silent gate visible. */
  function diag(reason: string): void {
    if (!diagnostics) return;
    const now = Date.now();
    if (now - lastDiagAt < DIAG_THROTTLE_MS) return;
    lastDiagAt = now;
    ctx.log(`blocked: ${reason}`);
  }

  function isMovementAbility(itemType: number): boolean {
    if (itemType <= 0) return false;
    const xml = ctx.gameData?.getRawObjectXml(itemType);
    return xml !== undefined && MOVEMENT_ACTIVATE_RE.test(xml);
  }

  /** MP the item actually costs; 0 when objects.xml does not say. */
  function abilityMpCost(itemType: number): number {
    const cached = mpCostCache.get(itemType);
    if (cached !== undefined) return cached;
    const xml = ctx.gameData?.getRawObjectXml(itemType);
    const match = xml ? MP_COST_RE.exec(xml) : null;
    const cost = match ? Math.max(0, Math.trunc(Number(match[1]) || 0)) : 0;
    mpCostCache.set(itemType, cost);
    return cost;
  }

  ctx.onGameDataReload(() => { mpCostCache.clear(); });

  function sendUseAbility(client: ClientConnection, usePos: { x: number; y: number }, itemType: number): void {
    const pkt = ctx.createPacket('USEITEM');
    pkt.data = {
      time: Math.trunc(client.time ?? 0),
      slotObject: { objectId: client.objectId, slotId: ABILITY_SLOT, objectType: itemType },
      itemUsePos: { x: usePos.x, y: usePos.y },
      useType: 1,
      unknownInt: 0,
    };
    pkt.modified = true;
    selfFiring = true;
    try { client.sendToServer(pkt); } finally { selfFiring = false; }
  }

  ctx.hookPacket('MAPINFO', (client, packet) => {
    const name = String(packet.data.name ?? '').toLowerCase();
    const display = String(packet.data.displayName ?? '').toLowerCase();
    const combined = name + ' ' + display;
    safeZone.set(client, SAFE_ZONE_SUBSTRINGS.some(s => combined.includes(s)));
    nextAllowedAt.delete(client);
  });

  // Manual ability press -> back off so we don't fight the player's cooldown.
  ctx.hookPacket('USEITEM', (client, packet) => {
    if (selfFiring) return;
    if (packet.data?.slotObject?.slotId === ABILITY_SLOT) {
      nextAllowedAt.set(client, Date.now() + MANUAL_PAUSE_MS);
    }
  });

  ctx.hookPacket('NEWTICK', (client) => {
    if (!ctx.enabled || !client?.connected || !client.objectId) return;
    if (safeZonePause && (safeZone.get(client) ?? true)) {
      diag('in a safe zone (or the map is not identified yet)');
      return;
    }

    const pd = client.playerData;
    const cls = pd.classType;
    const isTarget = TARGET_CLASSES.has(cls);
    const isSelf = SELF_CLASSES.has(cls);
    if (!isTarget && !isSelf) {
      diag(`class ${cls} is not an auto-castable class`);
      return;
    }

    const itemType = pd.inventory?.[ABILITY_SLOT] ?? -1;
    if (itemType <= 0) { diag('ability slot is empty'); return; }
    if (isMovementAbility(itemType)) { diag(`item ${itemType} moves the player`); return; }

    // maxMana is the gearless base (StatType.MaxMP); effectiveMaxMana adds the
    // gear and exalt bonuses, the same total auto-drink uses. The percentage
    // gate below is a reserve, not a floor on the full bar.
    const trueMaxMana = pd.effectiveMaxMana;
    if (trueMaxMana <= 0) { diag('max MP is not known yet'); return; }

    const mpCost = abilityMpCost(itemType);
    if (mpCost > 0 && pd.mana < mpCost) {
      diag(`MP ${pd.mana} is below the item's ${mpCost} cost`);
      return;
    }
    const mpPct = (pd.mana / trueMaxMana) * 100;
    if (mpPct < mpReservePct) {
      diag(`MP ${Math.round(mpPct)}% is under the ${mpReservePct}% reserve`);
      return;
    }

    const now = Date.now();
    const readyAt = nextAllowedAt.get(client) ?? 0;
    if (now < readyAt) { diag(`cooling down for another ${readyAt - now}ms`); return; }

    // pos {0,0} before first update looks like cheating to the server.
    if (pd.pos.x === 0 && pd.pos.y === 0) { diag('player position is still 0,0'); return; }

    const ws = ctx.getWorldState(client);
    const gd = ctx.gameData;
    if (!ws || !gd) { diag('world state / game data is not ready'); return; }

    // The floor is on MAX HP, not current, so a boss already worn down still
    // qualifies. Default 0 (any enemy); raise it to stop spending MP on trash.
    const targetFilter = {
      maxDistance: abilityRange,
      maxStaleMs: targetMaxStaleMs > 0 ? targetMaxStaleMs : undefined,
      maxHpMin: targetMinMaxHp > 0 ? targetMinMaxHp : undefined,
      excludeScenery: skipScenery,
    };

    /**
     * "No enemy in range" has four possible causes and they need different
     * fixes, so name which one it was instead of making the next test a guess.
     * Only runs while diagnostics are on.
     */
    function explainNoTarget(): string {
      if (!ws || !gd) return 'no world state';
      const all = ws.getEnemiesMatching(gd, pd.pos, { maxDistance: abilityRange });
      if (all.length === 0) {
        return `nothing enemy-classed within ${abilityRange} tiles (tracking ${ws.entityCount} entities)`;
      }
      const afterScenery = ws.getEnemiesMatching(gd, pd.pos, {
        maxDistance: abilityRange,
        excludeScenery: skipScenery,
      });
      if (afterScenery.length === 0) {
        return `all ${all.length} candidate(s) rejected as scenery — turn off "Skip walls / breakables"`;
      }
      const afterHp = ws.getEnemiesMatching(gd, pd.pos, targetFilter as any);
      const stale = afterScenery.filter((e) => {
        const tracked = ws.getEntity(e.objectId);
        return !tracked || (now - tracked.lastUpdate) > (targetMaxStaleMs || Infinity);
      });
      if (afterHp.length === 0 && stale.length > 0) {
        const oldest = Math.max(...stale.map((e) => {
          const t = ws.getEntity(e.objectId);
          return t ? now - t.lastUpdate : 0;
        }));
        return `${stale.length} of ${afterScenery.length} enemy(s) stale — oldest ${oldest}ms `
          + `vs the ${targetMaxStaleMs}ms window; raise it or set it to 0`;
      }
      if (afterHp.length === 0) {
        return `${afterScenery.length} enemy(s) in range but under the ${targetMinMaxHp} max-HP floor`;
      }
      return `${afterHp.length} enemy(s) passed every filter — target selection itself failed`;
    }

    if (isSelf) {
      // A self-buff burns the same MP whether anything worth buffing for is
      // nearby or not, so it can be held behind the same target test the aimed
      // classes use rather than cast into an empty room. Toggleable, because
      // some buffs are wanted before the fight starts.
      if (selfNeedsTarget && !ws.getNearestEnemy(gd, pd.pos, targetFilter)) {
        diag(`self-cast held: ${explainNoTarget()}`);
        return;
      }
      sendUseAbility(client, pd.pos, itemType);
      nextAllowedAt.set(client, now + selfIntervalMs);
    } else {
      const enemy = ws.getNearestEnemy(gd, pd.pos, targetFilter);
      if (!enemy) {
        diag(`no target: ${explainNoTarget()}`);
        return;
      }
      sendUseAbility(client, { x: enemy.x, y: enemy.y }, itemType);
      nextAllowedAt.set(client, now + targetIntervalMs);
    }
  });

  // The DLL-side auto-ability path is superseded by this packet approach.
  ctx.on('clientDisconnected', (client) => {
    safeZone.delete(client);
    nextAllowedAt.delete(client);
  });
}
