# Agent Notes

## 2026-04-15

- `cmake --build examples/FluentCFFZoneViewer/build-msys2-clang -j 8` 在沙箱内运行时出现超时，表现为长时间无返回并被会话超时中断。
- 该问题按沙箱限制处理：后续同类构建任务应切换到沙箱外执行（`require_escalated`）以完成编译与验证。
-测试文件位于 data\v21
- 不要调用 C:/ProgramData/anaconda3/ 下的任何库。默认库在 C:/tools/msys64/mingw64，没有库则安装
- agent 使用命令行直接运行 FluentCFFZoneViewer.exe 时输入测试文件
- 每次更改记得更新 doc下文档

## 2026-04-20 FluentCFFGNNPy：VS Code 与运行约定

- **vcpkg 安装前缀（MSVC）**：工作区 `.vscode/settings.json` 中 `fluentcff.gnn.vcpkgRoot` 指向 vcpkg 的 `.../installed/x64-windows`（本机示例：`E:/vcpkg-work/fluentcff-gnn/installed/x64-windows`）。CMake 预设里同名字段见 `cmake/FluentCFFGNNPy/CMakePresets.json`（`fluentcff-gnn-py-msvc` → `FLUENTCFF_MSVC_VCPKG_ROOT`）。
- **Pylance / 静态分析**：同一 `settings.json` 中配置 `python.analysis.extraPaths`，包含 `${workspaceFolder}/build/fluentcff_gnn_py`（`.pyd` 输出目录）、`${workspaceFolder}/python`，以及 `${config:fluentcff.gnn.vcpkgRoot}`，避免「无法解析导入 `fluentcff_gnn`」。
- **VS Code 任务**：`.vscode/tasks.json` 中 **FluentCFFGNNPy: smoke test (venv)** 通过 `FLUENTCFF_MSVC_VCPKG_ROOT=${config:fluentcff.gnn.vcpkgRoot}` 注入环境变量；`PATH` 含 `.venv/Scripts` 与 `torch/lib`。
- **终端直接跑 Python**：`python/smoke_test_fluentcff_gnn.py` 在未设置 `FLUENTCFF_MSVC_VCPKG_ROOT` 时，会尝试从上述 **CMakePresets.json** 读取默认前缀（且仅当 `.../bin` 存在），并在 Windows 上为 `torch/lib` 调用 `os.add_dll_directory` 且预挂 `PATH`，以便与任务行为接近、减少 `DLL load failed`。
- **仍以环境变量为准**：若本机 vcpkg 路径与 preset 不一致，请在 shell 或任务中设置 **`FLUENTCFF_MSVC_VCPKG_ROOT`**，并同步修改 `fluentcff.gnn.vcpkgRoot` / preset。