# FluentCFF Optimization Baseline Notes

## Run Record

- Date: 2026-04-15 06:09:17
- Branch/Commit: a3bc431
- Build Type: Debug
- Machine: Windows 10 (MSYS2 mingw64 runtime)
- Dataset:
  - Case: data/v21/step-gamma-20-eta-1_5.cas.h5
  - Data: data/v21/step-gamma-20-eta-1_5-x3-0_7.dat.h5

## Key Metrics

- `UpdateInformation`: 11 ms
- `Update` total: 57681 ms (previous recent run 69171 ms)
- `polyhedron vtkIdTypeArray ...`: 17652 ms
- `CreateFaceZonePolyData` (hot zones): `top1` 4 ms, `in` + `SV_DENSITY` 0-1 ms
- Peak memory (optional):

## Observations

- Face scalar path now avoids per-face chunk lookup and function call overhead.
- Loading path continues to be dominated by polyhedron build + bulk data assembly.

## Change Summary

- `CreateFaceZonePolyData` optimized by resolving `DataChunk` once and using direct indexed access.

## Next Iteration

- Reduce per-polyhedron temporary allocations in `RequestData` cell-type-7 path.
- Split `collect-log.ps1` output by latest run marker to avoid historical noise in summaries.
