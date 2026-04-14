# HISTORY

> 由各窗口代理维护：执行命令前先阅读其他窗口汇总；执行命令后追加当前窗口任务汇总与时间戳。

## Current Window - 2026-04-15 04:08:41
- Objective: 确认“每次运行按 CLI 输出日志”的具体诉求并准备调整方案。
- Actions: 阅读了 `HISTORY.md` 与 `AGENT.md`，并记录当前会话时间戳。
- Result: 已同步上下文与约束，待用户确认具体输出粒度后再改代码。
- Next: 给出包含项目树的改动思路并征求用户许可。

## Current Window - 2026-04-15 04:16:44
- Objective: 实现 Debug 模式命令行+日志文件双写，文件名按程序名 `-debug`。
- Actions: 修改 `main.cxx` 与 `vtkFLUENTCFFReader.cxx`，加入统一时间戳日志函数、环境变量日志路径和双写输出；执行了构建验证。
- Result: 代码编译到链接阶段通过，最终链接因 `FluentCFFZoneViewer.exe` 被占用失败（Permission denied）。
- Next: 关闭运行中的 `FluentCFFZoneViewer.exe` 后重新构建并运行验证日志落盘。

## Current Window - 2026-04-15 04:22:05
- Objective: 按用户要求重新运行构建验证日志改动。
- Actions: 重新执行 `cmake --build examples/FluentCFFZoneViewer/build-msys2-clang -j 8`。
- Result: 仍在最终链接阶段失败，`FluentCFFZoneViewer.exe` 被占用（Permission denied）。
- Next: 用户需先关闭正在运行的 `FluentCFFZoneViewer.exe`（或结束占用进程）后再重试构建。

## Current Window - 2026-04-15 04:37:19
- Objective: 验证项目启动后 Debug 日志是否持续输出并落盘。
- Actions: 检查 `FluentCFFZoneViewer` 进程状态；定位并读取 `FluentCFFZoneViewer-debug.log` 最新内容。
- Result: 程序运行中，日志文件存在且持续写入，包含时间戳及 viewer/reader 双来源日志。
- Next: 可继续按用户需要过滤特定函数耗时，或在会话结束后停止进程并归档日志。

## Current Window - 2026-04-15 04:54:02
- Objective: 针对严重性能瓶颈实现 reader 侧结构性优化。
- Actions: 在 `vtkFLUENTCFFReader` 增加 face zone 拓扑缓存与 cell-zone 索引；重写 `RequestData`/`ParseUDMData` 填充路径避免重复全量扫描；完成重编译并检查日志变化。
- Result: 构建通过；日志显示相同字段切换下 `CreateFaceZonePolyData` 多次调用从历史秒级/百秒级下降到毫秒级到亚秒级（首次构建仍较高，后续复用显著加速）。
- Next: 由用户在当前 GUI 会话继续操作并采集新一轮完整 `Update` 耗时，评估加载阶段总耗时下降幅度。

## Current Window - 2026-04-15 05:26:59
- Objective: 按用户要求增加 cas/dat 规模预估后的低风险预分配。
- Actions: 在 `vtkFLUENTCFFReader.cxx` 为 `Cells/Faces/FaceZones` 及常用子数组加入 `reserve`；在 metadata/data 读取路径提前 `reserve` 字段容器并复用 section 缓冲区；执行重编译验证。
- Result: 编译通过，改动保持现有逻辑语义，仅降低动态扩容频率与临时分配开销。
- Next: 运行同一测试工况，采集 `Update` 与字段切换耗时对比预分配前后的收益。

## Current Window - 2026-04-15 05:36:27
- Objective: 回答“原始 VTK 如何构建 cell”。
- Actions: 阅读 `vtkFLUENTCFFReader` 的 `RequestData`、`GetFaces/GetCells`、`Populate*Cell` 相关实现并梳理流程。
- Result: 明确其核心是“先按 face 拓扑重建 cell 节点顺序，再按 cell 类型调用 `InsertNextCell`”。
- Next: 向用户给出分步骤说明，并指出 polyhedron 与常规单元的差异。
