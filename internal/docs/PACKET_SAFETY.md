# Packet safety

Disconnects from invalid or bursty client traffic are a top priority. This doc
lists known send paths, limits, and the dodge unification plan.

## Branch context

- Base: `feat/auto-loot-tier-dropdowns` (4 commits ahead of `main`)
- Safety work: `fix/packet-safety-stability`

## Client proxy limits

`OutboundPacketGuard` (on `ClientConnection.sendToServer`) caps synthetic packets:

| Packet | Limit / sec |
|--------|-------------|
| USEITEM | 12 |
| INVENTORYSWAP | 6 |
| PLAYERHIT | 16 |
| OTHERHIT | 4 |
| ESCAPE | 4 |
| HELLO | 2 |
| All synthetic combined | 64 |

Plugins also keep their own budgets (auto-loot, auto-drink, auto-nexus escape).

**Bypass:** `ClientConnection.sendRawToServer()` does not pass through this guard
(auto-nexus releases held PLAYERHIT passthrough only).

## Plugin audit (synthetic `sendToServer` paths)

| Plugin | Packets | Local cap | Guard |
|--------|---------|-----------|-------|
| auto-drink | USEITEM | 8/sec, 1 pot/pass, 60ms poll | USEITEM 12 |
| auto-ability | USEITEM | interval ms per class | USEITEM 12 |
| auto-loot | INVENTORYSWAP, USEITEM | 3 actions/sec default | 6 swap / 12 use |
| auto-nexus | ESCAPE | retry interval + 4/sec align | ESCAPE 4 |
| admin-autododge | OTHERHIT (redirect) | 4/sec per client | OTHERHIT 4 |
| rollback | HELLO | manual reconnect only | HELLO 2 |
| GhostHit (DLL→DevServer) | PLAYERHIT | 16/sec + 2.5s dedup | PLAYERHIT 16 |

Passthrough only (no new C→S): anti-debuffs (blocks PLAYERHIT), spoof-push-tiles
(modifies UPDATE), auto-nexus (blocks AOEACK/GROUNDDAMAGE when nexusing).

## High-risk plugin paths

| Source | Packet | Risk | Mitigation |
|--------|--------|------|------------|
| auto-drink poll (60ms) + NEWTICK | USEITEM | High | `MAX_USEITEMS_PER_SEC=8`, in-flight ledger |
| auto-ability NEWTICK | USEITEM | Medium | Interval settings, `effectiveMaxMana` floor |
| auto-loot NEWTICK | INVENTORYSWAP, USEITEM | High | Stale slot checks, reservations, 3/sec cap |
| auto-nexus escape retry | ESCAPE | Medium | Retry cap + interval |
| auto-nexus threat eval | ESCAPE | Low | 50ms poll (was 20ms) |
| GhostHit (DLL) | PLAYERHIT | High | 16/sec + 2.5s dedup in DevServer |
| auto-drink poll + NEWTICK | USEITEM | High | 1 pot/pass, poll skips 150ms after tick |

## Dodge (DLL): MOVE burst — dodge code, packet-safety goal

Dodge does not use the TypeScript proxy. It calls `DangerPlanner::NativeMoveTo`,
which invokes the game's `MoveTo` and emits MOVE packets from the game process.

Problem: reflex layers (XDodge, Rollout, zDodge, RePP, PJDodge) can call
`NativeMoveTo` every render frame (~60 Hz). The server expects roughly one MOVE
per tick (~200ms). Sustained 30-60 MOVE/sec is a common disconnect trigger.

**This is dodge-path code** (only dodge engines call `NativeMoveTo`), but it
lives on this branch as **disconnect prevention**, not dodge feel / unified
planner work. Removing it restores MOVE floods whenever any dodge mode is on.
Movement feel (TryMoveToward, unified arbitration) belongs on `feat/dodge-overhaul`.

Fix on this branch: minimum interval between `NativeMoveTo` calls (default 40ms, ~25 MOVE/sec).

Long term: follow `internal/src/features/movement/dodge/DODGE_OVERHAUL_PLAN.md`.
Goal is one unified stack (BFS reflex + arrival-time A* strategy) behind toggles,
with PJDodge as the default dashboard mode until the overhaul lands.

## Dodge modes today

| Mode | DLL backend | Notes |
|------|-------------|-------|
| RE-Plus (xdodge) | XDodge.cpp | BFS + A*, most mature |
| RE-Sim grid/quad | RolloutDodge.cpp | Forward sim, A/B backends |
| zDodge | zDodge | Slide assist |
| RE++ | RePP | Reactive |
| PJDodge | PJDodge | CCD + survival-first (default on this branch) |

`auto-dodge.ts` is a settings bridge only. Unification is DLL-side work.

## Invalid inventory operations

Server drops the connection (not ignore) when:

- `INVENTORYSWAP` `slotObject2.objectType` does not match the real slot contents
- Same bag slot swapped twice before entity stats update
- Sustained `INVENTORYSWAP` / `USEITEM` flood

auto-loot guards: dest re-read, bag slot re-read, reservations, rate cap.

## Testing after changes

1. Stand on a multi-item bag with auto-loot on. Confirm no disconnect.
2. Take damage with auto-drink + auto-nexus both on.
3. Enable PJDodge in a dense bullet pattern. Confirm movement without kick.
4. Enable diagnostic logging on auto-loot if swaps still fail.
