# GTOS 原生 V8 UTC 探针

本模块验证固定版本 V8 的真实 `Time::Now()`、`Time::NowFromSystemTime()`、`Time::ToJsTime()` 和 `TimeTicks::Now()` 在 GTOS IA32 CPL3 中运行。UTC 来自已提交的 CMOS/PIT 0x4714 ABI，单调时钟继续使用原有 0x4712 ABI。

`v8-time-utc.patch` 只为已准备的 GTOS V8 `time.cc` 增加 UTC 分支；桥实现位于 `apps/native_v8_runtime/runtime/gtos-clock-bridge.cc`。补丁的输入和结果分别为 SHA256 `654b0b0cea8d06d8cecb330d941fc92ba43d6200e95538d51a18292e9401e123` 和 `4fcb7970902c01d69904bf4525c277f98afb93b2d8dc79d41a9a0b472641171a`。应用时在独立准备目录使用 `patch --batch --forward --fuzz=0 -p1`；不要修改原始 V8 checkout。

## 前置条件与构建

需要已经准备的私有 GTOS V8 overlay、LLVM C SDK 和 GN 生成头，以及来源锁固定的 Clang24、libc++ 和 GCC13.3。此入口检查 652 个准备目录文件和 100 个仓库输入的 SHA256；它不负责创建完整 V8 SDK，声明头也不代表服务已经实现。来源锁针对本次验收输入，内核或准备目录变更后必须重新验收。

```sh
python3 -B tools/build-v8-utc-probe.py /path/to/fresh-output \
  --prepared-v8 /path/to/prepared-v8 \
  --prepared-sdk /path/to/prepared-llvm-c-sdk \
  --prepared-gen /path/to/prepared-gn-gen \
  --libcxx-source /path/to/pinned-libcxx/src \
  --clang /path/to/pinned-clang/bin/clang \
  --memory-cxx /path/to/gcc13/bin/g++
```

默认构建 O0/O2 的四个程序和有效/不可用 RTC 测试内核，执行八次 QEMU 冷启动。环境需提供已有 GTOS GRUB/QEMU 工具及 `GTOS_QEMU_DATA_DIR`。传入 `--build-only` 时只构建、链接、审计并比对已验收 用户 ELF 以及内核入口、程序头和全部装载字节，manifest 的来宾执行标志保持 false；它通过来源锁引用之前实际执行的资格证明。工具拒绝覆盖现有输出，保留日志、依赖、栈分析和输入快照。

这是独立测试内核，不改变生产镜像。测试内核明确以 `NativeFpSse2` 激活已验收的 x87/SSE2 所有权管理，生产内核无需变更。

## 验收范围

QEMU 和官方未修改 Bochs2.6.11 各八次冷启动：O0/O2 ×32MiB/1配置CPU、64MiB/4配置CPU ×有效/实际不可用 RTC。原生执行仍在 BSP；配置4 CPU不表示多核原生任务。

- 40 个真实任务、56 次精确 Reap：正常退出、写入栈 guard 后 #PF14/error6、外部取消73及再次装载；完整8192字节保留页、原始页面基线、FP 初始化/保存/恢复/失效和整数/ring0/boot peer 继续运行。
- 真实方法返回值与 syscall 捕获值逐次对应；Python 独立日期解析和 PIT 有理数公式验证 UTC 锚及单调增量，实际 `ToJsTime()` 双精度结果误差不超过一 ULP。
- UTC 桥拒绝空指针、六项协议元数据异常及不属于 V8 有符号时间域的值。22 项程序内拒绝检查；零表示合法 Unix epoch，最大值哨兵不能被当作普通时间。
- 实际 RTC 不可用时，在有效 `TimeTicks` 调用后 UTC syscall 返回 -38，原有 fatal 路径产生 #UD6；输出保持不变，没有伪造时间或单调时间回退。
- 24 类独立观察器反例被拒绝；八个 ELF 的完整执行输入段通过明确 x87/SSE/SSE2 集合审计，并拒绝 SSE3、AVX、MMX、FXSAVE/XSAVE 反例。冻结探针最深保守栈界 O0 728、O2 444 字节，低于8176字节。

详细原始证据保存在 GTOS-Chromium 工作区 `artifacts/native-v8-utc-bridge-20261009-a`，包括最初遗漏 FP 激活参数的 #NM 失败及审计器修正记录。来源锁保存证据内容哈希、精确用户 ELF 哈希和内核装载哈希。

UTC 服务为启动 RTC 锚加实际送达 PIT IRQ，分辨率10000微秒，RTC秒相位不确定度1000000微秒。此模块没有时区/墙钟同步、丢失 IRQ 恢复或高分辨率保证。完整 V8 Isolate、Chromium 网页浏览、视频及 HTML5 尚未在 GTOS 运行。

GNU ld 会把测试内核中两个无文件名汇编对象的输出路径放入非装载 FILE 符号。构建目录改变时 .symtab/.strtab 会变；资格比对覆盖入口、所有程序头、完整 PT_LOAD 文件字节及 memsz/零填充边界，不把这些路径字符串当作运行字节。原始完整 ELF 哈希仍保留在来源锁中。
