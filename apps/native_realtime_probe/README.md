# 原生 UTC 验收程序

真正的独立 i386 ELF32/CPL3 程序，读取内核 RTC/PIT UTC 扩展，
同时验证既有单调接口、完整输出哨兵、跨页和重叠请求、寄存器及保留页面。

四个编译模式分别覆盖正常结束、真实 guard 页故障、保留资源时外部取消、
无效硬件 RTC 的 UNSUPPORTED 路径。该程序由 tools/verify-native-realtime.py
打包到独立验收镜像；不替换桌面启动载荷。协议与重现命令见 docs/native-realtime.md。
