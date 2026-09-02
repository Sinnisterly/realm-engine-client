# DLL compare tools

Static analysis helpers for MultiTool vs Realm Engine `version.dll`.

## Requirements

```bash
pip install pefile
```

## Commands

```bash
# PE analysis (pass both DLL paths on Windows)
export MULTITOOL_DLL="/path/to/MultiTool_Data/version.dll"
export REALMENGINE_DLL="/path/to/client/assets/version.dll"
python3 analyze_pe.py "$MULTITOOL_DLL" "$REALMENGINE_DLL"

# NameMapping -> RuntimeOffsets report
python3 map_namemapping.py > namemapping_report.md
```

## Outputs

- [`../docs/MULTITOOL_DLL_COMPARISON.md`](../docs/MULTITOOL_DLL_COMPARISON.md) — full write-up
- `realm_engine_hooks.json` — hooks from our source tree
- `multitool_hooks.json` — hooks inferred from MultiTool strings/PE
- `namemapping_report.md` — generated mapping table
