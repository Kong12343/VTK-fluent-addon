from __future__ import annotations

from importlib.resources import files


def get_api_reference_path() -> str:
    """Return the installed API reference Markdown path."""
    return str(files(__package__).joinpath("vtkIOFLUENTCFF_api_reference.md"))
