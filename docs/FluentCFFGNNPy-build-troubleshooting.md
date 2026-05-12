# FluentCFFGNNPy 编译与运行排障说明

本文档汇总在 **Windows + MSVC（Ninja 或 VS 生成器）+ 仓库 `.venv`（pip torch / pybind11）+ vcpkg（VTK / HDF5，x64-windows）** 下构建与加载 `fluentcff_gnn` 扩展时常见问题：**现象 → 原因 → 处理**。实现以仓库内 [`cmake/FluentCFFGNNPy/CMakeLists.txt`](../cmake/FluentCFFGNNPy/CMakeLists.txt)、[`python/smoke_test_fluentcff_gnn.py`](../python/smoke_test_fluentcff_gnn.py)、[`.vscode/tasks.json`](../.vscode/tasks.json) 为准。

**可选 Python 依赖**：[`python/fluentcff_field_utils.py`](../python/fluentcff_field_utils.py) 中的 `read_dat_case_basename` 需要 **`h5py`**（`pip install h5py`）；与扩展模块本身无关。

---

## 1. 环境与工具链约定（先读）

| 约定 | 说明 |
|------|------|
| **编译器 ABI** | pip 安装的 PyTorch C++ 库为 **MSVC ABI**。扩展必须用 **MSVC（cl）** 构建；**不要用 MinGW g++ 与 pip torch 混链**（CMake 会在检测到 MinGW + Windows 时直接报错退出）。 |
| **Python** | 默认使用仓库根目录 **`.venv`**（可用 `-DFLUENTCFF_VENV_ROOT=...` 覆盖）。`Python_EXECUTABLE` 被 **FORCE** 指向该 venv 的 `python.exe`，避免 `FindPython` 从 `CMAKE_PREFIX_PATH` 捡到 **MSYS2 Python**（ABI/头文件错误）。 |
| **VTK / HDF5（MSVC）** | 必须通过 **`FLUENTCFF_MSVC_VCPKG_ROOT`** 指向 vcpkg 的 **`.../installed/x64-windows`**。可与 CMake 参数 `-D` 或环境变量 **`FLUENTCFF_MSVC_VCPKG_ROOT`** 传入（MSVC 下若 cache 为空会从环境读入并写入 cache）。 |
| **CUDA** | 仅当 venv 的 `torch/lib` 下存在 **`torch_cuda.dll`**（等 wheel CUDA 标记）时，CMake 才强制解析 **CUDA Toolkit**（`cuda.h` / `nvcc`）。**CPU-only** wheel 不会要求本机安装 CUDA。 |

推荐配置入口：在 **`cmake/FluentCFFGNNPy`** 目录使用 [`CMakePresets.json`](../cmake/FluentCFFGNNPy/CMakePresets.json) 或 VS Code 任务 **FluentCFFGNNPy: CMake Configure (preset)** / **Build (preset)**（`cwd` 指向该子目录，以便 `cmake --preset` 生效）。

---

## 2. CMake：`find_package(Torch)` 成功但链接报 `Torch::Torch` 不存在

**现象**：配置通过，生成阶段链接目标写成 `Torch::Torch` 时报目标未定义。

**原因**：**pip** 安装的 torch 所带的 `TorchConfig.cmake` 通常提供导入目标 **`torch`**（小写），而不是 `Torch::Torch`（部分 LibTorch 独立发行版命名）。

**处理**：[`CMakeLists.txt`](../cmake/FluentCFFGNNPy/CMakeLists.txt) 中优先 **`if(TARGET torch)`** 链接 `torch`，否则再回退 `Torch::Torch`。

---

## 3. MSVC 配置仍混入 MSYS2 / MinGW 的 VTK 或头文件爆炸

**现象**：包含路径中出现 `C:/tools/msys64/mingw64/...`，编译报错涉及 MinGW 特有头文件、内联汇编或与 MSVC 不兼容的声明。

**原因**：`PATH` 或历史 **`VTK_DIR`** / `CMAKE_PREFIX_PATH` 指向 **MSYS2 MinGW** 的 VTK；与 **MSVC + vcpkg VTK** 混用会导致头文件与运行库不一致。

**处理**：

- CMake 对 MSVC 将 **`C:/tools/msys64/mingw64`** 及相关 `lib/cmake` 路径加入 **`CMAKE_IGNORE_PREFIX_PATH`**。
- 若 **`VTK_DIR`** 路径匹配 `msys64`，则 **`unset(VTK_DIR CACHE)`** 并重新在干净 build 目录配置。
- 删除旧的 **`build/fluentcff_gnn_py`**（或所用 binaryDir）后重新 **`cmake` + `cmake --build`**，避免 cache 残留。

---

## 4. HDF5：MSVC 下误用 MSYS 的 `libhdf5.dll.a`

**现象**：在 MSVC 工程里仍去链 MSYS 的 HDF5 导入库，或找不到与 VTK 匹配的 HDF5。

**原因**：FluentCFF reader 依赖 HDF5；**MSVC 扩展**必须与 **vcpkg x64-windows** 的 HDF5/VTK 一致。

**处理**：MSVC 分支使用 **`find_package(hdf5 CONFIG REQUIRED)`**，链接 **`hdf5::hdf5-shared`** 或 **`hdf5::hdf5-static`**（见 CMakeLists）。非 Windows 使用 **`find_package(HDF5)`** + `HDF5::HDF5`，与 MSYS MSVC 路径分离。

---

## 5. CUDA：配置阶段报缺少 `cuda.h`，但只想用 CPU torch

**现象**：`find_package(Torch)` 或 Caffe2 的 `cuda.cmake` 要求 CUDA，本机未装 Toolkit。

**原因**：当前 venv 安装的是 **带 CUDA 的 pip torch**（存在 `torch_cuda.dll` 等），CMake 会走 CUDA 探测分支。

**处理**（二选一）：

- 安装与 wheel 匹配的 **NVIDIA CUDA Toolkit**，并保证 **`FLUENTCFF_CUDA_ROOT` / `CUDA_PATH`** 与 **`nvcc`** 可被 CMake 找到（CMakeLists 中有 Program Files 默认路径与 PATH 探测逻辑）。
- 或改用 **CPU-only** wheel：例如 `pip install --force-reinstall torch --index-url https://download.pytorch.org/whl/cpu`（以官方说明为准），使 `torch/lib` 下不再出现 CUDA 标记文件，CMake 将跳过 CUDA 强制 pinning。

---

## 6. CMake 警告：`CMAKE_CUDA_ARCHITECTURES` 与 `TORCH_CUDA_ARCH_LIST`

**现象**：配置时出现 PyTorch 提示：`CMAKE_CUDA_ARCHITECTURES` 将被忽略，应设置 **`TORCH_CUDA_ARCH_LIST`**。

**原因**：PyTorch 的 CMake 脚本自行管理 NVCC `-gencode` 参数。

**处理**：一般可忽略；若需固定架构，按 PyTorch 文档设置 **`TORCH_CUDA_ARCH_LIST`**（例如 `7.5`），而不是仅依赖 CMake 的 `CMAKE_CUDA_ARCHITECTURES`。

---

## 7. CMake 警告：`library kineto not found`

**现象**：`TorchConfig.cmake` 警告 kineto 未找到。

**原因**：部分 pip 发行包未附带该分析库。

**处理**：通常**不影响**扩展链接与运行；若后续需要 profiling 再单独处理。

---

## 8. C++ / pybind：`Update` 绑定失败（MSVC 重载）

**现象**：`pybind11` 绑定 `vtkFLUENTCFFReader::Update` 时模板推导失败或指向错误重载。

**原因**：`vtkAlgorithm` 上 **`Update` 有多个重载**，取成员函数指针在 MSVC 上不明确。

**处理**：在 [`python/fluentcff_gnn_pybind.cpp`](../python/fluentcff_gnn_pybind.cpp) 中使用 **lambda**：`.def("Update", [](vtkFLUENTCFFReader& r) { r.Update(); })`。

---

## 9. C++ / pybind：`GetReader` 与 VTK 对象所有权

**现象**：使用 `std::unique_ptr<vtkFLUENTCFFReader>` 等默认 holder 时，与 VTK **非 public 析构** 或所有权模型冲突，编译或设计期报错。

**原因**：Python 侧不应“拥有”由 `FluentCFFGNNExporter` 内部 `vtkSmartPointer` 持有的 reader。

**处理**：Reader 类型以 **`py::class_<..., std::unique_ptr<..., py::nodelete>>`** 暴露；`GetReader` 返回 **`std::unique_ptr<..., py::nodelete>(e.GetReader())`**。C++ 侧 Exporter 内使用 **`vtkSmartPointer<vtkFLUENTCFFReader>`**（见 [`FluentCFFGNN/FluentCFFGNNExporter.h`](../FluentCFFGNN/FluentCFFGNNExporter.h)）。

---

## 10. 链接：`LNK2001` 无法解析 `pybind11::detail::type_caster<at::Tensor,...>::cast`

**现象**：链接 `fluentcff_gnn*.pyd` 时提示无法解析外部符号，符号带 `__declspec(dllimport)`，与 **`at::Tensor`** 的 pybind 转换相关。

**原因**：[`torch/csrc/utils/pybind.h`](https://github.com/pytorch/pytorch) 中 `type_caster<at::Tensor>` 的实现标记为 **`TORCH_PYTHON_API`**，位于 **`torch_python`** 动态库，**仅链 `torch.lib` 不够**。

**处理**：

- 在 **`torch/lib`** 下 **`find_library(torch_python ...)`** 并加入 **`fluentcff_gnn`** 的 **`target_link_libraries`**。
- 保留 **`TORCH_EXTENSION_NAME=fluentcff_gnn`** 的编译定义（与 `torch/extension.h` 扩展约定一致）。

见 [`CMakeLists.txt`](../cmake/FluentCFFGNNPy/CMakeLists.txt) 中 `_FLUENTCFF_TORCH_PYTHON_LIB` 与 `TORCH_EXTENSION_NAME`。

---

## 11. 运行：`ImportError: DLL load failed`（找不到指定模块）

**现象**：`import fluentcff_gnn` 失败，即使已将 `torch/lib` 加入系统 `PATH`。

**原因**（Windows + Python **3.8+**）：扩展所依赖的 DLL 搜索路径需通过 **`os.add_dll_directory`** 显式注册；**仅改 `PATH` 对解释器加载 `.pyd` 的依赖链不可靠**。

**处理**：

- 在导入前调用与 [`smoke_test_fluentcff_gnn.py`](../python/smoke_test_fluentcff_gnn.py) 中相同的逻辑：注册 **venv 的 `Lib/site-packages/torch/lib`**、**`FLUENTCFF_MSVC_VCPKG_ROOT/bin`**（VTK、HDF5 等）、以及 **`CUDA_PATH`（或 `CUDA_HOME`）下的 `bin`**（若使用 CUDA torch）。
- VS Code 任务 **FluentCFFGNNPy: smoke test (venv)** 在 [`tasks.json`](../.vscode/tasks.json) 中为进程设置 **`FLUENTCFF_MSVC_VCPKG_ROOT=${config:fluentcff.gnn.vcpkgRoot}`** 与 **`CUDA_PATH=${env:CUDA_PATH}`**；若系统未设置 `CUDA_PATH`，可在任务 `env` 中写死本机 CUDA 安装路径。

---

## 12. 运维：磁盘空间、vcpkg 使用错误 `cmake`

**现象**：在 F: 等设备上 vcpkg 构建失败（空间不足）；或 vcpkg 日志显示调用了 **MSYS 下的 `cmake.exe`** 导致异常。

**处理**：

- 将 vcpkg 的 downloads/buildtrees/packages/installed 迁到有空间的路径（例如 E:），可使用仓库内脚本 **[`cmake/FluentCFFGNNPy/install-vcpkg-deps-E.ps1`](../cmake/FluentCFFGNNPy/install-vcpkg-deps-E.ps1)**（详见脚本注释与 `HISTORY.md` 中相关记录）。
- 在 **VS Developer PowerShell** 中执行长时 vcpkg；确保 **`cmake` 来自 Visual Studio / Kitware**，PATH 中 **不要** 让 MSYS `cmake` 排在前面。

---

## 13. 产物位置与验证

- 默认将 **`fluentcff_gnn*.pyd`** 输出到 **CMake binary 目录**（预设下多为仓库 **`build/fluentcff_gnn_py`**）。可用 cache 变量 **`FLUENTCFF_PYD_OUTPUT_DIRECTORY`** 覆盖。
- 最小验证：在设置好环境变量后运行 **`python/smoke_test_fluentcff_gnn.py`**（需 `data/v21` 下示例 cas/dat）。

更多 Python API 说明见同目录下的 [`fluentcff_gnn_module.md`](fluentcff_gnn_module.md)。

---

## 14. 训练依赖：`torch-geometric` 与 PyG 二进制轮

**场景**：运行 [`python/train_baseline_graphsaint.py`](../python/train_baseline_graphsaint.py) 或图网络相关代码，需 **PyTorch Geometric**（及 `torch-scatter` / `torch-sparse` 等与当前 **torch + CUDA** 匹配的 wheel）。

**处理**：

1. 先在本机 `.venv` 安装与硬件一致的 **`torch`**（CPU 或 CUDA wheel；见 [PyTorch Get Started](https://pytorch.org/get-started/locally/)）。
2. 再安装可选清单 **[`python/requirements-train.txt`](../python/requirements-train.txt)** 中的 **`torch-geometric`**。
3. 若 `pip` 无法解析 **`torch-scatter`** 等扩展，按 [PyG Installation](https://pytorch-geometric.readthedocs.io/en/latest/install/installation.html) 使用 **`https://data.pyg.org/whl/`** 上与本机 **`torch-*` + `cu*`/`cpu`** 一致的额外包（示例：`pip install pyg_lib torch_scatter torch_sparse torch_cluster torch_spline_conv -f https://data.pyg.org/whl/torch-2.5.0+cu124.html`，版本号请自行替换）。

**说明**：训练脚本在未安装 PyG 时会报错并提示上述步骤；扩展模块 **`fluentcff_gnn`** 本身仍仅需 `torch`。

**GraphSAINT / NeighborLoader**：若未安装 **`torch-sparse`**（或 **`pyg-lib`**），PyG 的 `GraphSAINTNodeSampler` 与 `NeighborLoader` 不可用；[`python/train_baseline_graphsaint.py`](../python/train_baseline_graphsaint.py) 会自动退化为 **随机节点子图**（无需稀疏后端）。安装与当前 torch 版本匹配的 **`torch-sparse`** 后可启用完整采样器（见 PyG 官方 wheel 索引）。
