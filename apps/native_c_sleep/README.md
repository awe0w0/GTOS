# GTOS 原生 C nanosleep

此组件把固定 LLVM C SDK 的真实 nanosleep 接入 GTOS ABI1 的单调时钟与 yield。time_t 和 timespec 两个字段均为有符号 64 位。实现按经过时间判断完成，不把时长加到截止时间，也不把完整时长乘成纳秒，因此接受 tv_sec=INT64_MAX、tv_nsec=999999999 的有效请求。

请求为空返回 -1/EFAULT；负秒、负纳秒或纳秒不小于十亿返回 -1/EINVAL。真实时钟及 yield 错误通过已验收 C 时钟组件写入 LLVM TLS errno；检测到时间退回起点以前时返回 -1/EIO。零时长不读取时钟、不调度，成功保留 errno。

请求先复制到局部对象，余量可以与请求共用地址。ABI1 尚无原生用户信号，不产生 EINTR，也不写余量。非空请求必须是完整可读的 C 对象；非法映射遵循普通 C 访问语义，可触发由内核包含的用户 #PF。此实现反复 yield 等待真实 PIT 时间推进，实际返回可能晚于请求；单调时钟分辨率仍为 10ms。

## 构建与复现

使用已验收 C 时钟模块的锁定 SDK、libc++、Clang24、GCC13.3、IA32 整数 helper archive，并配置 GRUB/QEMU 工具路径：

~~~sh
python3 -B tools/build-native-c-sleep-probe.py /path/to/fresh-output \
  --clang /path/to/pinned-clang/bin/clang \
  --libcxx-source /path/to/pinned-libcxx/src \
  --memory-cxx /path/to/qualified-gcc13/bin/g++ \
  --integer-builtins /path/to/qualified/libscalar_integer.a
~~~

默认重新编译 O0/O2 并执行四次冷启动。输出目录必须全新。来源锁固定本模块、整个伴随 C 时钟模块及其旧 SDK 依赖；构建要求服务对象、八个 ELF 和两个内核的完整装载内容与已执行资格一致。--build-only 仅重编译和比对已有资格，明确不产生新的来宾通过结果。

运行库档案包含 nanosleep、clock_gettime、真实 LLVM errno、emutls、配套 VM heap 和标量内存实现。用于完整引擎时应选择这组提供者，替换旧重复实现。它不扩展旧运行库整库资格，也不提供完整 POSIX 信号、时区或通用多线程。

## 已执行验收

QEMU O0/O2 × 32MiB/1配置CPU、64MiB/4配置CPU，共四次冷启动、20 个真实 CPL3 任务、24 次精确 Reap。配置四 CPU 不表示多核原生执行。

每个任务读取实际 syscall 的 PIT tick，独立按 ticks × 11931 × 1000000 / 1193182 向下取整核对 C timespec 及实际经过时间。覆盖零时长、1ns、微秒边界、10ms边界、跨页请求与余量共址、非法参数、真实时钟错误传播、正常退出、请求 guard #PF14/error4，以及两种极限时长的外部取消73。

298 个边界和固定随机向量使用 Python 任意精度整数生成独立期望值。实际 IA32 helper 从 volatile 对象取值执行，每个任务均比较，共5960次。所有用户页、64KiB私有堆和FP所有权按既有内核生命周期回收，其他任务持续运行。

76 个错误观测反例和五类禁止 ISA 反例均被拒绝。八个 C ELF 及四个真实 Abseil ELF 的全部实际执行输入段通过 legacy x87/SSE/SSE2 审计。C 探针最大保守调用栈 O0 为1616、O2为1248字节，低于8176；整数 helper 的栈记录来自与实际 archive 成员逐字一致的编译对象。

导出 O2 nanosleep 对象与实际 GN 对象逐字一致。另以固定上游 Abseil clock/duration/time/int128、真实默认 LLVM printf、GTOS TLS及C服务执行两次冷启动：10 个任务、120 个计时样本、12 次精确 Reap，包含真实 SleepFor 和正常RTC路径的 GetCurrentTimeNanos、Now。此诊断没有替换上游调用；它不构成完整 Abseil 或 printf 资格。

默认 printf 的 long double 转换仍有8364字节单帧，超过当前用户栈，尚未完成完整验收。完整原生 V8 Isolate、Chromium网页浏览、视频和HTML5仍未运行。生产镜像及可见虚拟机保持已验收的PNG桌面；内核容量未改变。
