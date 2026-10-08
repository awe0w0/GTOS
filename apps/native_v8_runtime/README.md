# GTOS 原生 V8 运行库诊断模块

此模块保存已经在 GTOS CPL3 中运行验收的 IA32 运行库子集，供后续完整 V8/Chromium 原生适配使用。当前使用私有 libc++ ABI 命名空间 `__gtos_isolate_diagnostic`，必须显式启用诊断构建。

目前包含64KiB VM-backed heap、new/delete、compiler-rt emutls 控制块适配、C++ TLS 析构注册、实际 GTOS 单调与 CMOS/PIT UTC 时钟桥，以及17个 libc++ 单任务锁/一次初始化/线程身份/yield/睡眠 ABI helper。程序通过 `gtos_native_exit_with_tls` 正常退出时执行 TLS 析构；内核故障和外部取消直接回收整个私有地址空间，不执行用户析构。

GTOS ABI1 每个进程地址空间只有一个原生任务。这些实现依赖真实 PROCESS_INFO 验证该能力，不支持共享地址空间线程。条件变量、epoch timed wait、thread create/join/detach、TLS key、完整 std::mutex/call_once 库、C atexit/静态析构和 C++ 异常仍未闭合。私有 SDK 头文件提供真实声明，不能当作缺失服务的实现。完整 V8 Isolate、Chromium 浏览、视频及 HTML5 尚未运行。

## 构建

在 WSL/Linux 中，准备来源锁指定的 Clang、GCC13.3及 libc++ 源码。SDK 依赖头随模块保存，保留“私有覆盖 → libc++ → LLVM C SDK”的包含顺序。

```sh
python3 -B tools/build-native-v8-runtime.py /path/to/fresh-output \
  --clang /path/to/pinned-clang/bin/clang \
  --libcxx-source /path/to/pinned-libcxx/src \
  --memory-cxx /path/to/qualified-gcc13/bin/g++ \
  --enable-diagnostic
```

输出为 `libgtos_native_runtime_diagnostic.a`、无未解析符号的 `runtime-closure.o` 以及构建 manifest/日志。工具拒绝覆盖既有输出；不依赖宿主 libc/libstdc++，不生成生产 ISO，也不更改 GTOS 内核容量。

`source-lock.json` 固定导出源码、SDK 头、编译器、libc++ revision、实际 GN 编译参数及已执行对象的 SHA256。构建必须得到与已验收对象逐字节相同的八个对象，才能复用原有来宾资格证明。输入路径、头文件和工具另行记录，构建前后哈希必须保持。

## 已执行验收

C++ TLS 与线程核心为两组独立证据，每组均在 QEMU 与未修改官方 Bochs2.6.11 中完成 O0/O2 ×32MiB/1配置CPU、64MiB/4配置CPU，共八次冷启动、32任务和40次精确 Reap。原生调度仍为 BSP 单任务，配置4 CPU不表示多核原生执行。

- TLS：正常析构顺序为身份 → 第二对象 → 析构中新建对象 → 第一对象；真实 OOM 注册失败保持已有链。输出 guard #PF 和活任务取消时存在真实对象/析构链/堆，Reap 精确恢复页面。18个反例自审通过。
- 线程核心：146项检查/任务，共4672项；递归深度、重复锁、非法解锁、once仅执行一次、真实单调睡眠及持锁故障/取消回收通过。21个反例自审通过。
- 冻结测试回调的保守 O0/O2 栈界限：TLS 7274/3936，线程核心4478/2352字节，低于8176字节限制。通用递归回调没有该栈保证。
- libc++ 当前 errno.h 为缺失的 EDEADLK 提供9975；该诊断 ABI 使用此实际值，不假定 Linux 的35，不声明完整 GTOS/POSIX errno 映射。

来宾证明的内容哈希、配置和计数见来源锁。完整本机证据位于 GTOS-Chromium 工作区的 `artifacts/native-cxx-tls-guest-20261008-a` 和 `artifacts/native-thread-core-guest-20261008-a`，旧失败及缓存保留。

## 来源

堆和标量内存函数来自 GTOS 现有已验收模块。emutls 参考 compiler-rt commit `62397f8b3c3986f54187ce08f00b3448ea1f8880`。SDK 使用 LLVM libc commit `ebe33e01982dbbf879661e3b6b78450f3020a53f`，libc++ commit `97b436da4c33663581d394f4ee0a5977fc38c2f4`；文件来源和原始哈希均保存在来源锁。LLVM 文件保留原版权头，并附 LLVM-LICENSE.txt。

## UTC 桥补充验收

UTC 使用独立0x4714协议，验证固定元数据及 V8 有符号时间域，保留原单调桥的全部源码前缀。RTC不可用或协议异常沿原有 fatal 路径退出，不伪造墙钟。实际 V8 Time/ToJsTime/TimeTicks 接入补丁、CPL3探针和严格构建入口见 [UTC探针](../native_v8_utc_probe/README.md)。

QEMU和官方未修改Bochs共16次冷启动、40个任务、56次精确Reap及24类观察器反例通过。新导出的桥对象与真实执行对象逐字相同；原来的七个其他运行库对象保持原哈希。更新后的来源锁保存新资格和旧桥对象哈希，旧TLS/线程证据仍对应其原始构建。运行库仍为八个对象，未扩大完整V8资格。