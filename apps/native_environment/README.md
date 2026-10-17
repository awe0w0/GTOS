# 原生启动环境与 CPU 计时

完整 V8 扩展根实际启动时，在环境初始化的第一次系统调用后触发了入口陷阱。
原因是 SDK 已调用 `0x4717`，当前内核还没有环境读取处理。本模块把已验证的
环境快照和 CPU 计时并入当前事件内核，保留事件等待、文件句柄复制及既有配额。

## 接口

`include/process/environment_abi.h` 定义版本 1 的 `ENVIRONMENT_READ`，调用号
`0x4717`，请求 16 字节，结果 4096 字节。可信内核在 CreateElf 时选择启动环境；
原有重载默认提供合法空环境，不导入宿主变量。最多 64 个唯一、非空名称条目，
数据区 3824 字节，允许空值和包含等号的值。条目、未用偏移和尾部均有明确边界。

`NativeEnvironment::Assign` 在完整验证后才替换旧值，支持自身数据别名。
读取先快照整个请求，再检查版本、flags、输出大小及整段可写范围；失败不写输出，
重叠和未对齐输出按原契约处理。worker 读取进程 root 的同一不可变快照，不能指定
其他进程。查询不分配、不调度；回收清空槽位。没有用户修改环境的接口。

`include/process/cpu_clock_abi.h` 定义 `CPU_CLOCK_READ`，调用号 `0x4716`，
请求 20 字节、结果 48 字节。线程与进程 scope 都只读调用方；进程计数包含已经
join 的 worker。仅 BSP PIT IRQ 采样到 Running 原生线程时记账，睡眠、boot/kernel
任务和不由 timer 触发的调度不增加计数。

单位为微秒，分辨率 10000 微秒，实际换算保留 PIT 分数：
`microseconds = floor(charged_ticks * 11931000000 / 1193182)`。
它不提供亚 tick 插值、漏 IRQ 补偿或通用 SMP CPU 使用率；不等同于 uptime/UTC。
计数溢出保持错误，不回绕；读取保留非结果寄存器且不改变计数或调度。

`sdk/` 保存实际使用的环境 bootstrap 和 LLVM app 配置适配。消费者必须在全局
构造函数之前调用 bootstrap；其余 LLVM 源码、libc 配置和固定工具由来源锁引用。
本模块没有宣称所有应用已具备统一 CRT，或可变 setenv/putenv/unsetenv 已支持。

## 原生验收

组合回归使用与实际 V8 原根相同的 Task/Runtime 类布局。复用经过完整 MD 和输入
终验的 17 个内核 C++ 对象、3 个汇编对象，按新头重编两个真实观察器；原用户
ELF、SDK getenv/EnvironmentManager/Perfetto 提供者和依赖均重新核对来源。

- O2 × 32MiB/smp1、64MiB/smp4 的环境/CPU 回归，每组 32 个原生上下文。
  覆盖九次 bootstrap、八组实际 SDK/色彩/构造用例、126 次环境拒绝、112 次 CPU
  拒绝、独立 PIT 有理计数、worker/peer 隔离、join/槽位重用、整组页故障和回收。
- 同两配置的当前事件回归，每组 9 个原生上下文。保留 reset、权限、WaitAny、
  快照页释放、关闭取消、PIT 回绕、resleep 隔离、32 对象/64 句柄及 peer 引用用例。
- 共 82 个上下文，包含预期故障情形；FP 初始化/失效相等，无不变量失败。
  每组物理帧精确回到各自基线，事件句柄和对象最终归零。
- 两个内核都对 35 个真实输入对象完成完整指令审查；整数归档的 14 个原始成员
  逐个验证并显式参与 map，没有跳过归档或增加 FP 豁免。

旧观察器曾被定时器调度出的 syscall 采样插入字符，导致 `ENV SDK` 记录拆行。
本版只给 CPU STOP、ENV SDK、CPU REAP 三个完整记录加短临界区；去掉这些
scope 和缩进后，与原文件逐字相同。所有原断言、采样、等待、调度和回收不变，
宿主完整记录校验也未放宽。旧失败保留于工作区，并在 provenance 中说明。

同一 1256 输入的原始扩展根另完成两配置、各连续两次真实 `_start/NativeEntry`
启动，保留两个构造函数和原 main。四次均 stage4/result0/exit0、无故障、JS mask127、
GC2，实际共 28 组原 JS 工作负载；独立大块清零，18 个停止地址空间页留到 Reap，
两次不增长，随后帧/任务/FP 精确恢复。fini 数组实际为空，不据构造数量推断析构数量。

这些是新合并 ABI 的 O2 证据，不把旧模块的 O0 结果继承到新布局，也不因配置了
4vCPU 宣称用户 SMP。V8 原根通过不代表 FilePath 全 API、PMA 类/文件 flush、
Chromium Content、网页、视频或 HTML5 已运行。

## 来源与回放

acceptance 和 source-lock 绑定公开源码与实际回执；probe 是实际观察器和用户代码，
logs 保存原始客体日志，receipts 保存完整终验/隔离/退出/指令结果。事件观察器
仍为[既有事件模块](../native_events/probe/kernel.cpp)的原文件。root-status 是晋升
之前的历史启动证据；其中旧 canonical 祖先源码由 Git 基线和工作区原件副本保留。

replay 是已执行入口快照，固定引用工作区工具、候选视图、用户 ELF 和提供者缓存，
不是可任意换路径执行的通用构建器，也不声明空白主机冷构建已通过。历史祖先
哈希不同于后来的规范源码时必须重新建立版本化来源视图，不能关闭检查。

所有执行遵守[本地命令栅栏](../../docs/native-command-execution.md)：最小环境、
固定工具摘要、Python -I -B -S、静态参数数组、无网络命名空间、Landlock 精确
执行文件白名单，仅写新输出。未改生产镜像，未重启生产虚拟机；镜像实际更新时
再同步可见虚拟机。

后续继续原 strnlen guard 页、完整 FilePath/PMA 类和真实文件映射/flush 后端，
再接入 Content 本地帧/DOM、HTTP/HTTPS、视频和 HTML5。原生浏览器仍未运行。
