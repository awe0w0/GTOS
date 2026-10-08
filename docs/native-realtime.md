# 原生 RTC/PIT UTC 读取

这是 32 位原生进程的独立只读扩展。它为 Chromium/V8 的后续时间接口提供真实内核服务，
尚未接入完整 V8 Isolate 或浏览器。原有 CLOCK_READ 仍只支持单调时钟；其 REALTIME 选择仍返回 -38。

内核在 BSP 激活原生运行时、IF 清除时，只读取 PC CMOS 的日期、模式和有效位。
连续两份快照必须一致，期间不能出现更新进行标志。最多尝试 1024 次；不写 CMOS 数据或模式。
接受 BCD/二进制、12/24 小时格式，采用 Gregorian 闰年规则，支持 1970–9999 年。
世纪来自寄存器 0x32；要求有效电池位、运行中的 32kHz 分频模式，拒绝 SET/DST 和非法日期。
此 PC 配置要求 CMOS 由平台按 UTC 初始化；内核无法从寄存器判断当地时区。

UTC 微秒值为有效 RTC 秒级锚点，加上激活后实际送达 BSP 的 PIT 单调时间差。
分辨率为 10000 微秒，RTC 秒相位不确定度为 1000000 微秒。没有亚秒 RTC 相位读取、
漏失 IRQ 补偿、时钟同步或设置接口。缺少有效 RTC 时，新接口返回 -38，原生进程和单调接口仍能工作。
拒绝单调时间回退；加法溢出返回 -75。

## 线协议

见 [realtime_abi.h](../include/process/realtime_abi.h)。int 0x80 输入 EAX=0x4714，
EBX=请求用户地址，ECX=16；返回 EAX=0 或负错误。其余寄存器沿用现有原生返回契约。

请求为四个 32 位字：version=1、flags=0、result_va、result_bytes=48。
结果前六个 32 位字为 version、unit、source、capabilities、resolution_us、anchor_uncertainty_us；
随后是三个 64 位值：microseconds、monotonic_microseconds、delivered_ticks。
source=1 表示 CMOS/PIT；capabilities=15 明确包含 RTC 已验证、UTC、粗粒度和实际 IRQ 计数。

校验顺序为请求大小、完整请求快照、版本、flags/结果大小、完整可写结果范围、
一致单调快照与 UTC 算术、结果复制。错误输出保持原值；请求和结果重叠受到支持。
请求页必须可读，结果页必须可写。地址回绕、内核范围、代码只读页、保护页和跨不可写页都被拒绝。

## 可复现验证

    CXX=/absolute/path/to/g++ python3 tests/native_realtime_calendar.py /fresh/calendar --sanitizers
    CXX=/absolute/path/to/g++ python3 tools/verify-native-realtime.py /fresh/guest

日历测试由 Python datetime/calendar 独立生成 4612 个向量，每个构建执行 27729 项检查；
O0/O2 i386 测试无 libc 链接，64 位测试支持 ASan/UBSan。它只验证纯整数逻辑，
实际 CPL3/syscall/PIT/回收证据由来宾入口提供。

来宾默认运行 QEMU 的 O0/O2 ×32MiB/1配置CPU、64MiB/4配置CPU矩阵。
CMOS 固定为 2000-01-01 UTC；另外的验收内核在激活前临时注入 DST 模式并立刻恢复，
证明实际服务保持 UNSUPPORTED，且单调读取仍正常。该注入只存在于测试内核。

正式 UTC 来宾还验证 18 类拒绝及 1072 字节输出哨兵、跨页和重叠结果、返回寄存器、
独立 IRQ 有理数换算、私有 CPL3/CR3、正常退出、真实 guard #PF6 和外部取消。
每个停止进程及最后一个独立 peer 都要求恢复精确空闲帧基线，ring0/boot/CPL3 peer 继续运行。
用户页、区域、进程、FP 槽与 8176 字节用户栈上限均未扩容。

可用 --emulator both --bochs-root /qualified/root --bochs-runner /qualified/runner 加入独立 Bochs。
提供的 runner 必须识别 NATIVE REALTIME SMOKE PASS/FAIL，且保留真实日志。
驱动只为 Bochs 子进程设置 TZ=UTC；Bochs 2.6.11 的本地时区 CMOS 初始化必须按此配置。
生产内核不接受用户时钟输入，也没有测试模式 syscall。
