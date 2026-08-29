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
| ESCAPE | 4 |
| All synthetic combined | 24 |

Plugins also keep their own budgets (auto-loot, auto-drink).

## High-risk plugin paths

| Source | Packet | Risk | Mitigation |
|--------|--------|------|------------|
| auto-drink poll (60ms) + NEWTICK | USEITEM | High | `MAX_USEITEMS_PER_SEC=8`, in-flight ledger |
| auto-ability NEWTICK | USEITEM | Medium | Interval settings, `effectiveMaxMana` floor |
| auto-loot NEWTICK | INVENTORYSWAP, USEITEM | High | Stale slot checks, reservations, 3/sec cap |
| auto-nexus escape retry | ESCAPE | Medium | Retry cap + interval |
| auto-nexus threat eval | ESCAPE | Low | 50ms poll (was 20ms) |

## Dodge (DLL): main burst source

Dodge does not use the TypeScript proxy. It calls `DangerPlanner::NativeMoveTo`,
which invokes the game's `MoveTo` and emits MOVE packets from the game process.

Problem: reflex layers (XDodge, Rollout, zDodge, RePP, PJDodge) can call
`NativeMoveTo` every render frame (~60 Hz). The server expects roughly one MOVE
per tick (~200ms). Sustained 30-60 MOVE/sec is a common disconnect trigger.

Fix on this branch: minimum interval between `NativeMoveTo` calls (default 100ms).

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
