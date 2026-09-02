#!/usr/bin/env python3
"""PE import/export/section analysis for version.dll comparison.

Usage:
  python3 analyze_pe.py <path-to.dll> [<path-to.dll> ...]

Environment:
  MULTITOOL_DLL   default path for MultiTool version.dll
  REALMENGINE_DLL default path for Realm Engine version.dll
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

try:
    import pefile
except ImportError:
    print("Install pefile: pip install pefile", file=sys.stderr)
    sys.exit(1)

HOOK_MARKERS = {
    "detours_text": [b"Detour", b"detour"],
    "minhook_text": [b"MinHook", b"MH_CreateHook", b"MH_Initialize"],
    "imgui_text": [b"ImGui", b"imgui"],
    "il2cpp_text": [b"il2cpp_", b"GameAssembly.dll"],
    "multitool_text": [b"MultiTool", b"RealmStock", b"NameMapping::"],
    "realmengine_text": [b"LFGInternalHelloReady", b"IpcBridge", b"BootGate", b"PJDodge"],
}


def extract_strings(data: bytes, min_len: int = 8) -> list[str]:
    out: list[str] = []
    cur = bytearray()
    for b in data:
        if 32 <= b <= 126:
            cur.append(b)
        else:
            if len(cur) >= min_len:
                out.append(cur.decode("ascii", errors="ignore"))
            cur.clear()
    if len(cur) >= min_len:
        out.append(cur.decode("ascii", errors="ignore"))
    return out


def marker_hits(strings: list[str], raw: bytes) -> dict[str, bool]:
    hits: dict[str, bool] = {}
    blob = b"\n".join(s.encode("ascii", errors="ignore") for s in strings) + raw
    for key, needles in HOOK_MARKERS.items():
        hits[key] = any(n in blob for n in needles)
    return hits


def analyze(path: Path) -> dict:
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(
        directories=[
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"],
        ]
    )

    sections = [
        {
            "name": s.Name.decode("ascii", errors="ignore").rstrip("\x00"),
            "virtual_size": int(s.Misc_VirtualSize),
            "raw_size": int(s.SizeOfRawData),
        }
        for s in pe.sections
    ]

    imports: list[str] = []
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode("ascii", errors="ignore")
            imports.append(dll)
            for imp in entry.imports:
                if imp.name:
                    imports.append(f"  {dll}!{imp.name.decode('ascii', errors='ignore')}")

    exports: list[str] = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if sym.name:
                exports.append(sym.name.decode("ascii", errors="ignore"))

    raw = path.read_bytes()
    strings = extract_strings(raw)
    feature_keywords = [
        "AutoAim", "WeaponModsProjectileNoclip", "SpeedHackDetector",
        "ObscuredCheatingDetector", "SlowWalk", "FpsVsync", "PJDodge",
        "ImGui", "NameMapping::", "MultiTool", "RealmStock", "BootGate",
        "IpcBridge", "GhostHit", "AutoNexus",
    ]
    feature_hits = sorted({s for s in strings if any(k in s for k in feature_keywords)})

    return {
        "path": str(path),
        "size_bytes": path.stat().st_size,
        "machine": hex(pe.FILE_HEADER.Machine),
        "timestamp": int(pe.FILE_HEADER.TimeDateStamp),
        "sections": sections,
        "exports": sorted(exports),
        "import_dlls": sorted({e for e in imports if not e.startswith("  ")}),
        "import_count": len([e for e in imports if e.startswith("  ")]),
        "markers": marker_hits(strings, raw),
        "feature_strings": feature_hits[:80],
        "string_count_ge8": len(strings),
    }


def main() -> None:
    paths: list[Path] = [Path(p) for p in sys.argv[1:]]
    if not paths:
        env_mt = os.environ.get("MULTITOOL_DLL")
        env_re = os.environ.get("REALMENGINE_DLL")
        for p in (env_mt, env_re):
            if p and Path(p).is_file():
                paths.append(Path(p))

    if not paths:
        print(__doc__, file=sys.stderr)
        sys.exit(2)

    results = [analyze(p) for p in paths]
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
