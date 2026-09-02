#!/usr/bin/env python3
"""Map MultiTool NameMapping logical names to Realm Engine RuntimeOffsets entries."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMEFILE = Path(__file__).resolve().parent / "multitool_namemapping.txt"
RUNTIME_OFFSETS = ROOT / "src/core/runtime/RuntimeOffsets.cpp"
BEEbyte = ROOT / "src/game/symbols/BeebyteName.h"

# MultiTool logical name -> (our obfuscated class, field tryNames from RuntimeOffsets)
# Hand-curated from RuntimeOffsets.cpp + il2cpp class aliases used in features/.
LOGICAL_TO_REALM = {
    ("basicMapObjectClass", "x"): ("KJMONHENJEN", "CLFEOFKBNEJ", "PosX"),
    ("basicMapObjectClass", "y"): ("KJMONHENJEN", "PKEECFNFEIO", "PosY"),
    ("basicMapObjectClass", "objectType"): ("KJMONHENJEN", "HFDNHJFNEKA", "ObjType"),
    ("basicMapObjectClass", "objectProperties"): ("KJMONHENJEN", "OBAKMCCDBJA", "ObjProps"),
    ("mapObjectClass", "hp"): ("LKHPPBEGNOM", "KJNHLADHEMH", "HP"),
    ("mapObjectClass", "maxHp"): ("LKHPPBEGNOM", "NCBIICBDGAG", "MaxHP"),
    ("mapObjectClass", "conditions"): ("LKHPPBEGNOM", "COHCKAPOLCA", "MoConditions"),
    ("mapObjectClass", "moveVec"): ("LKHPPBEGNOM", "ECGPFJKCCAN", "MoVelocity"),
    ("playerClass", "shooting"): ("FKALGHJIADI", "(shooting class)", "AimHooks player/shoot"),
    ("playerClass", "speed"): ("FKALGHJIADI", "CGCMALPMMJL / move speed", "SpeedHack"),
    ("objectPropertiesClass", "isEnemy"): ("ObjectProperties", "isEnemy", "OP_IsEnemy"),
    ("objectPropertiesClass", "maxHitPoints"): ("ObjectProperties", "(via XML)", "OP + stats"),
    ("objectPropertiesClass", "projectiles"): ("ObjectProperties", "Projectiles", "OP_Projectiles"),
    ("projectilePropertiesClass", "lifetime"): ("ProjectileProperties", "Lifetime", "PP_Lifetime"),
    ("projectilePropertiesClass", "speed"): ("ProjectileProperties", "ProjectileSpeed", "PP_Speed"),
    ("projectilePropertiesClass", "speedClamp"): ("ProjectileProperties", "SpeedClampValue", "PP_SpeedClamp"),
    ("projectilePropertiesClass", "accelDelay"): ("ProjectileProperties", "AccelerationDelayValue", "PP_AccelDelay"),
    ("projectilePropertiesClass", "accelValue"): ("ProjectileProperties", "AccelerationValue", "PP_Acceleration"),
    ("projectilePropertiesClass", "isAccel"): ("ProjectileProperties", "IsAccelerating", "PP_IsAccel"),
    ("projectilePropertiesClass", "accelInv"): ("ProjectileProperties", "AccelerationInv", "PP_AccelerationInv"),
    ("projectileClass", "damagesEnemies"): ("HBEAKBIHANL", "DBNNDLKNECM", "Hbeak_InstanceDamage"),
    ("projectileClass", "_square"): ("KJMONHENJEN", "EOKJOGFPLOA", "ProjNoclip tile ptr"),
    ("mapViewServiceClass", "player"): ("HJMBOMEHGDJ", "OCLNLBHDEFK", "WM_Local"),
    ("mapViewServiceClass", "mapObjectDictionary"): ("HJMBOMEHGDJ", "DFALIKKKGLI", "WM_AllDict"),
}


def load_namemapping() -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    for line in NAMEFILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        cls, field = line.split("::", 1)
        rows.append((cls, field))
    return rows


def runtime_offset_classes() -> set[str]:
    text = RUNTIME_OFFSETS.read_text(encoding="utf-8")
    return set(re.findall(r'\{\s*"([A-Za-z0-9_]+)"', text))


def main() -> None:
    classes_in_offsets = runtime_offset_classes()
    mapped = 0
    unmapped: list[str] = []
    only_multitool: list[str] = []

    print("# NameMapping vs RuntimeOffsets\n")
    print("| MultiTool logical | Realm class | Realm field / note | RuntimeOffsets row |")
    print("|---|---|---|---|")

    for cls, field in load_namemapping():
        key = (cls, field)
        if key in LOGICAL_TO_REALM:
            rc, rf, note = LOGICAL_TO_REALM[key]
            has = "yes" if rc in classes_in_offsets else "class not in table"
            print(f"| `{cls}::{field}` | `{rc}` | `{rf}` | {note} ({has}) |")
            mapped += 1
        else:
            unmapped.append(f"{cls}::{field}")

    print("\n## MultiTool-only logical fields (no direct RuntimeOffsets row yet)\n")
    for row in unmapped:
        print(f"- `{row}`")
        only_multitool.append(row)

    print(f"\nMapped: {mapped}/{mapped + len(unmapped)}")
    print(f"MultiTool-only: {len(only_multitool)}")


if __name__ == "__main__":
    main()
