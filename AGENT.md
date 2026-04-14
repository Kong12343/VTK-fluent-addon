# Agent Notes

## 2026-04-15

- `cmake --build examples/FluentCFFZoneViewer/build-msys2-clang -j 8` 在沙箱内运行时出现超时，表现为长时间无返回并被会话超时中断。
- 该问题按沙箱限制处理：后续同类构建任务应切换到沙箱外执行（`require_escalated`）以完成编译与验证。
-测试文件位于 data\v21
- 不要调用 C:/ProgramData/anaconda3/ 下的任何库。默认库在 C:/tools/msys64/mingw64，没有库则安装
- agent 使用命令行直接运行 FluentCFFZoneViewer.exe 时输入测试文件