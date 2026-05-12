# vtk-addon API Reference

This package replaces `vtkmodules.vtkIOFLUENTCFF` with a locally built version
from `vtk/IO/FLUENTCFF` and exposes a Python class named
`vtkmodules.vtkIOFLUENTCFF.vtkFLUENTCFFReader`.

## Module

- Module name: `vtkmodules.vtkIOFLUENTCFF`
- Primary class: `vtkFLUENTCFFReader`

## vtkFLUENTCFFReader

Reader for Fluent Common Fluids Format files (`.cas.h5` and `.dat.h5`).

### Constructor

#### `vtkFLUENTCFFReader()`

Create a new reader instance.

### File configuration

#### `SetFileName(file_name: str) -> None`

Set the Fluent case file path. This should usually point to a `.cas.h5` file.

#### `GetFileName() -> str`

Return the currently configured case file path.

#### `SetDataFileName(data_file_name: str) -> None`

Set an explicit Fluent data file path, typically a `.dat.h5` file.
If not set, the underlying C++ reader may derive the data file name from the case file.

#### `GetDataFileName() -> str`

Return the currently configured data file path.

### Array handling

#### `SetRenameArrays(rename_arrays: int) -> None`

Enable or disable array renaming.

- `0`: keep original Fluent-style names
- `1`: rename arrays to more descriptive names when supported

#### `GetRenameArrays() -> int`

Return the current rename-arrays flag.

#### `SetExcludedFieldArrayNames(names: list[str]) -> None`

Provide field-array names that should be excluded when reading data chunks.
Names must match the names expected by the reader after any rename policy is applied.

#### `GetExcludedFieldArrayNames() -> list[str]`

Return the excluded field-array name list currently stored in the reader.

#### `ClearExcludedFieldArrayNames() -> None`

Clear the excluded field-array name list.

### Execution

#### `Update() -> None`

Execute the reader pipeline and load mesh/data according to the configured files and options.

### Face-zone queries

#### `GetNumberOfFaceZones() -> int`

Return the number of face zones currently known to the reader.
Before `Update()`, this is typically `0`.

#### `GetFaceZoneName(index: int) -> str`

Return the face-zone name for a zero-based face-zone index.
Returns an empty string if the underlying reader returns `nullptr`.

#### `GetFaceZoneIdByName(name: str) -> int`

Return the Fluent zone id for a face-zone name.
Returns the C++ reader's sentinel value when the zone is not found.

#### `GetFaceZoneType(zone_id: int) -> int`

Return the face-zone type integer for a Fluent zone id.
Typical examples include wall and symmetry zone types.

### Cell-zone queries

#### `GetCellZoneType(zone_id: int) -> int`

Return the cell-zone type integer for a Fluent zone id.
Typical values distinguish fluid and solid zones.

## Notes

- The Python binding currently exposes a focused subset of the full C++ `vtkFLUENTCFFReader` API.
- The authoritative implementation lives in:
  - `vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h`
  - `vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx`
- For interactive help after installation, use:

```python
import vtkmodules.vtkIOFLUENTCFF as m
help(m)
help(m.vtkFLUENTCFFReader)
```

- To find this installed Markdown file from Python:

```python
from vtk_addon_docs import get_api_reference_path
print(get_api_reference_path())
```
