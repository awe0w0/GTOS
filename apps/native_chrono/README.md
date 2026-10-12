# GTOS 原生 chrono 时钟

本模块由完整 IA32 V8 第 28 次最终链接的 `chrono::steady_clock::now()` 首错驱动。
真实消费者为 Abseil `KernelTimeout(Duration)`。接线使用固定 libc++ 的 chrono
算法，仅为 GTOS 选择已经实现的 `clock_gettime` 后端；同时保留固定源码的
无异常 `__throw_system_error` 诊断和终止行为。源码覆盖与实际 GN 接线位于
`apps/native_v8_integration/overlay/tools/gtos_isolate_diagnostic/`。

`steady_clock` 读取真实 PIT 单调时钟并返回纳秒；`system_clock` 读取原生 RTC
锚定时间并返回固定 libc++ 的微秒表示。不用墙上时钟替代单调时钟，不为缺失的
`timespec_get` 返回成功，也不改变 GTOS 的平台身份。原 chrono 算法和已有
原生时钟提供者保持原行为。两个新对象没有动态初始化，最大单函数栈帧为 36/0 字节。

## 资格与边界

实际 GN 对象已在 32 MiB/1 CPU 与 64 MiB/4 CPU 的两次私有冷启动执行。
当前 BSP 单用户任务契约下，10 个任务完成 40 次时钟单位核对，12 次回收
恢复准确帧基线。观察器读取真实生产 syscall 结果，核对 PIT 纳秒、RTC 微秒、
不变 epoch 锚、实际流逝时间、寄存器与帧数量，并覆盖故障、取消和随后恢复。

错误路径在测试 IRQ 门关闭时暂时触发现有生产计数器的 exhausted 状态，由真实
`ReadClock` 与 syscall 派发返回错误，随后恢复生产者状态；没有替换 syscall 结果。
当前固定 LLVM C SDK 的 `EOVERFLOW` 为 9940，现有 panic 前缀令 abort 退出码为
`0x48000021`。工具从实际输入生成这些预期，不套用 Linux 数字。原始夹具身份
发布竞态和 ABI 数字假设的两次失败均保留，提供者未因夹具修正而改变。

自审绑定全部实际源码和对象，核对 ELF 数据地址与帧成本，拒绝 172 个失真观察。
完整第 29 次实际链接已越过 chrono，5137 条旧对象命令、原顺序链接输入及参数
保持一致，1264 份输入已冻结；真实下一首错为 `mbsrtowcs`。

## 重放

先构建同一完整原生目标并完成实际最终链接尝试，再在固定 Linux/WSL 环境执行：

```sh
python3 -B tools/verify-native-chrono.py /path/to/fresh-output \
  --integration-build /path/to/complete-native-output \
  --integer-builtins /path/to/libscalar_integer.a \
  --cxx /path/to/qualified-g++ \
  --qemu /path/to/qualified-qemu-system-i386 \
  --firmware /path/to/qualified-qemu-firmware
```

工具使用实际完整目标对象及冻结源，生成 ABI 预期与 ELF 几何，绑定实际程序、
加载器、共享库、固件和源码。内核编译器为已验收的 GCC 13.3.0。每次使用新
输出，保留失败和缓存；生成的私有测试镜像不会替换生产虚拟机的介质或磁盘。
`acceptance.json` 记录本次资格，不能替代新环境的实际重放。

没有新增多线程支持或容量扩展。完整 ELF、客体 Isolate/JS、浏览器、视频和
HTML5 尚未验收，本模块资格不能代替这些最终目标。
