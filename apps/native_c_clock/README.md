# GTOS 原生 C 时钟与 LLVM TLS errno

此组件把真实 0x4712 单调时钟和独立 0x4714 CMOS/PIT UTC 接入实际 LLVM C SDK 的 `clock_gettime`。私有 GTOS 选择值为 CLOCK_MONOTONIC=1、CLOCK_REALTIME=2；time_t 为有符号64位，timespec 的秒和纳秒字段各64位。完整无符号64位微秒值先验证元数据，再转换为秒/纳秒；不使用 V8 Max 哨兵限制。分辨率仍为10ms，UTC锚的秒相位不确定度仍为1秒。

失败返回-1并写真实 LLVM thread_local errno：非法选择EINVAL、空指针EFAULT、坏元数据/未知服务错误EIO、RTC不可用ENOSYS=9942、原生溢出EOVERFLOW=9940。后两个值与固定 libc++ 的现有回退值一致。成功保留 errno；服务失败保持输出。非空输出必须是完整可写的 C 对象，非法映射遵循普通 C 内存访问语义，可能触发部分写入及内核包含的用户 #PF，不能承诺对所有指针原子返回EFAULT。

LLVM errno 源码来自固定 ebe33e01982dbbf879661e3b6b78450f3020a53f，以模式2和实际 compiler-emulated TLS 编译。storage 由真实 VM heap 提供，直到 Reap 回收。分配核心不访问errno，malloc/calloc/realloc 在失败出口报告错误；posix_memalign 返回错误值并保留errno。GTOS ABI1 每个私有进程只有一个任务；未实现共享地址空间的多线程。

## 构建与复现

先准备锁定的 Clang24、libc++、GCC13.3及已验收 IA32 整数 helper archive，并配置 GRUB/QEMU 工具路径：

```sh
python3 -B tools/build-native-c-clock-probe.py /path/to/fresh-output \
  --clang /path/to/pinned-clang/bin/clang \
  --libcxx-source /path/to/pinned-libcxx/src \
  --memory-cxx /path/to/qualified-gcc13/bin/g++ \
  --integer-builtins /path/to/qualified/libscalar_integer.a
```

默认重编译并运行八组实际 CPL3 来宾。--build-only 仅重编译、链接，要求四个服务对象、八个用户ELF及四个内核的全部装载内容与真实执行资格一致，明确不产生新的来宾通过结果。输出目录必须全新；来源锁保存本模块及伴随旧SDK文件、工具、输入和资格的SHA256。

运行库组件包含新分配器、C时钟、LLVM errno、emutls和标量内存实现。用于完整引擎时须选择这些实际组件，替换旧 native_v8_runtime archive 的 heap/errno实现，避免重复定义。旧八对象运行库档案及其 TLS/线程/UTC资格保留原始含义；不能把新组件的验收扩大成旧整库的新验收。

## 已执行验收

QEMU及未修改官方Bochs2.6.11各八次冷启动：O0/O2 ×32MiB/1配置CPU、64MiB/4配置CPU ×有效/不可用RTC，共40任务、56精确Reap。配置4 CPU不表示多核原生执行。

验证了真实clock_gettime及实际Highway Start、固定元数据拒绝、完整uint64转换、errno成功/失败规则、首次及重复TLS访问、分配失败、跨页timespec、正常退出、guard #PF14/error6、取消73、缺失RTC输出保持、完整保留页和64KiB堆/FP所有权回收。每次独立捕获实际syscall，按PIT IRQ有理数公式和Python Gregorian历法核对。

独立Python oracle对524个边界/随机向量，在实际i386/x64 O0/O2解码函数上逐字节核对4192次解码。28个观察器反例及五类ISA反例通过；八个ELF所有实际执行输入段经legacy x87/SSE/SSE2审计。编译器除法helper新增栈记录后的对象与实际archive成员逐字相同；最大保守调用栈O0 1452、O2 1012字节，低于8176。

实际GN选择的Abseil clock.cc与Highway timer.cc对象已成功编译；Abseil日志/格式化、nanosleep等服务仍待闭合。此组件不提供时区、完整POSIX、完整libc++或浏览器。完整原生Isolate、Chromium网页浏览、视频和HTML5尚未运行。生产镜像及可见虚拟机保持已有已验收PNG桌面。

LLVM源码保留版权头，许可见LLVM-LICENSE.txt；Highway保存本次真正调用的固定上游头及原版权头，来自同一锁定V8依赖树。
