# FluentCFF perf-opt Workflow

This folder contains a dedicated optimization workflow for `FluentCFFZoneViewer`.

## Files

- `build-debug.ps1`: Build Debug executable in `build-msys2-clang`.
- `run-v21.ps1`: Launch viewer with `data/v21` benchmark case/dat files.
- `collect-log.ps1`: Snapshot debug log and extract key timing lines.
- `baseline-notes.md`: Template for recording iterative optimization results.

## Quick Start (PowerShell)

```powershell
pwsh ./build-debug.ps1
pwsh ./run-v21.ps1
pwsh ./collect-log.ps1
```

## Output Locations

- Runtime log: `examples/FluentCFFZoneViewer/build-msys2-clang/FluentCFFZoneViewer-debug.log`
- Snapshots: `examples/FluentCFFZoneViewer/build-msys2-clang/perf-opt-reports/`

## Notes

- Scripts force MSYS2 toolchain binaries to the front of `PATH`:
  `C:/tools/msys64/mingw64/bin`
- `run-v21.ps1` defaults to:
  - `data/v21/step-gamma-20-eta-1_5.cas.h5`
  - `data/v21/step-gamma-20-eta-1_5-x3-0_7.dat.h5`
