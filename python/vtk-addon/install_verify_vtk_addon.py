from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


MODULE_NAME = "vtkmodules.vtkIOFLUENTCFF"
EXPECTED_DOC = "vtkmodules.vtkIOFLUENTCFF replaced by local vtk/IO/FLUENTCFF"


def run_python_json(python_exe: str, code: str) -> dict:
    completed = subprocess.run(
        [python_exe, "-c", code],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"Python subprocess failed with exit code {completed.returncode}:\n"
            f"STDOUT:\n{completed.stdout}\nSTDERR:\n{completed.stderr}"
        )
    return json.loads(completed.stdout)


def inspect_module(python_exe: str, extra_sys_path: list[str] | None = None) -> dict:
    code = r"""
import hashlib
import importlib
import importlib.util
import json
import os
from pathlib import Path
import sys

module_name = "vtkmodules.vtkIOFLUENTCFF"
extra_sys_path = __EXTRA_SYS_PATH__
for entry in reversed(extra_sys_path):
    if entry not in sys.path:
        sys.path.insert(0, entry)
try:
    import vtkmodules
    for entry in reversed(extra_sys_path):
        vtkmodules_dir = str(Path(entry) / "vtkmodules")
        if Path(vtkmodules_dir).exists() and vtkmodules_dir not in vtkmodules.__path__:
            vtkmodules.__path__.insert(0, vtkmodules_dir)
except Exception:
    pass

if os.name == "nt":
    dll_candidates = []
    env_root = os.environ.get("FLUENTCFF_MSVC_VCPKG_ROOT")
    if env_root:
        dll_candidates.append(str(Path(env_root) / "bin"))
    dll_candidates.append("E:/vcpkg-work/fluentcff-gnn/installed/x64-windows/bin")
    for candidate in dll_candidates:
        try:
            if Path(candidate).exists():
                os.add_dll_directory(candidate)
        except Exception:
            pass

spec = importlib.util.find_spec(module_name)
result = {
    "module_name": module_name,
    "found": spec is not None,
    "origin": None,
    "origin_exists": False,
    "origin_sha256": None,
    "doc": None,
    "reader_ok": False,
    "error": None,
    "reader_checks": {},
}
if spec is not None:
    try:
        origin = spec.origin
        result["origin"] = origin
        if origin:
            path = Path(origin)
            result["origin_exists"] = path.exists()
            if path.exists():
                result["origin_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()

        module = importlib.import_module(module_name)
        result["doc"] = getattr(module, "__doc__", None)

        reader = module.vtkFLUENTCFFReader()
        reader.SetFileName("case.cas.h5")
        reader.SetDataFileName("case.dat.h5")
        reader.SetRenameArrays(1)
        reader.SetExcludedFieldArrayNames(["SV_P", "SV_T"])
        checks = {
            "file_name": reader.GetFileName(),
            "data_file_name": reader.GetDataFileName(),
            "rename_arrays": reader.GetRenameArrays(),
            "excluded_before_clear": reader.GetExcludedFieldArrayNames(),
        }
        reader.ClearExcludedFieldArrayNames()
        checks["excluded_after_clear"] = reader.GetExcludedFieldArrayNames()
        checks["face_zone_count_before_update"] = reader.GetNumberOfFaceZones()
        result["reader_checks"] = checks
        result["reader_ok"] = (
            checks["file_name"] == "case.cas.h5"
            and checks["data_file_name"] == "case.dat.h5"
            and checks["rename_arrays"] == 1
            and checks["excluded_before_clear"] == ["SV_P", "SV_T"]
            and checks["excluded_after_clear"] == []
            and isinstance(checks["face_zone_count_before_update"], int)
        )
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"

print(json.dumps(result, ensure_ascii=True))
"""
    code = code.replace("__EXTRA_SYS_PATH__", json.dumps(extra_sys_path or []))
    return run_python_json(python_exe, code)


def install_wheel(python_exe: str, wheel: Path, target: Path | None) -> None:
    if target is not None:
        if target.exists():
            shutil.rmtree(target)
        target.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(wheel) as zf:
            zf.extractall(target)
        return

    cmd = [
        python_exe,
        "-m",
        "pip",
        "install",
        "--upgrade",
        "--force-reinstall",
        "--no-deps",
    ]
    if target is not None:
        target.mkdir(parents=True, exist_ok=True)
        cmd.extend(["--target", str(target)])
    cmd.append(str(wheel))
    env = os.environ.copy()
    temp_root = wheel.parent.parent / ".verify-tmp"
    temp_root.mkdir(parents=True, exist_ok=True)
    env["TMP"] = str(temp_root)
    env["TEMP"] = str(temp_root)
    subprocess.run(cmd, check=True, env=env)


def summarize_change(before: dict, after: dict) -> dict:
    return {
        "origin_changed": before.get("origin") != after.get("origin"),
        "hash_changed": before.get("origin_sha256") != after.get("origin_sha256"),
        "doc_matches_expected": after.get("doc") == EXPECTED_DOC,
        "reader_ok": after.get("reader_ok", False),
    }


def main() -> int:
    default_wheel = Path(__file__).resolve().parent / "dist" / "vtk_addon-0.1.0-cp312-cp312-win_amd64.whl"
    parser = argparse.ArgumentParser(
        description="Install vtk-addon wheel and verify vtkIOFLUENTCFF replacement before/after."
    )
    parser.add_argument("--python", default=sys.executable, help="Target Python executable.")
    parser.add_argument("--wheel", default=str(default_wheel), help="Path to vtk-addon wheel.")
    parser.add_argument(
        "--target",
        default=str(Path(__file__).resolve().parent / ".verify-site-packages"),
        help="Install target for verification. Use an empty string to install into the interpreter environment.",
    )
    parser.add_argument(
        "--skip-install",
        action="store_true",
        help="Only inspect current state and run reader checks without installing.",
    )
    args = parser.parse_args()

    python_exe = str(Path(args.python).resolve())
    wheel = Path(args.wheel).resolve()
    target = Path(args.target).resolve() if args.target else None

    if not args.skip_install and not wheel.exists():
        raise FileNotFoundError(f"Wheel not found: {wheel}")

    before = inspect_module(python_exe)
    install_error = None
    if not args.skip_install:
        try:
            install_wheel(python_exe, wheel, target)
        except subprocess.CalledProcessError as exc:
            install_error = f"CalledProcessError: {exc}"
    after = inspect_module(python_exe, [str(target)] if target is not None else None)

    report = {
        "python": python_exe,
        "wheel": str(wheel),
        "target": str(target) if target is not None else None,
        "skip_install": args.skip_install,
        "expected_doc": EXPECTED_DOC,
        "install_error": install_error,
        "before": before,
        "after": after,
        "summary": summarize_change(before, after),
    }

    print(json.dumps(report, indent=2, ensure_ascii=True))
    if install_error is not None:
        return 1
    if not report["summary"]["doc_matches_expected"] or not report["summary"]["reader_ok"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
