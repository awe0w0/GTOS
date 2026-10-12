# GTOS 原生时间格式化

本模块接入完整 IA32 V8 目标的真实 libc++ 时间输出消费者所需的 `strftime`。
提供者为固定 LLVM-libc 的未修改 `strftime.cpp` 与 `time_utils.cpp`，通过
`apps/native_v8_integration/overlay/tools/gtos_isolate_diagnostic/BUILD.gn` 接线。
实际使用的 31 项新增源文件和头依赖已纳入源码锁；原引擎参数和容量限制保持不变。

当前语言环境为 C；非 Linux 的固定提供者按 UTC 输出时区名称和偏移。
格式化读取调用者提供的 `tm`，本模块没有新增实时时钟或时区数据库服务。
完整转换保留日期名称、复合格式、ISO 周数、数值宽度与缓冲区溢出行为，
不会用缩减格式集合替代实际消费者。公共 `strftime` 和内部算法均来自固定源码。

## 验证范围

私有资格验证使用真实 GN 命令生成的对象，在 32 MiB/1 CPU 与 64 MiB/4 CPU
两次冷启动中执行实际 libc++ `no_locale` 包装调用。当前 BSP 单用户线程契约下，
12 个任务完成正常退出、目标缓冲/格式/日期指针故障、取消和随后恢复，
14 次回收检查恢复准确帧基线，整数内核与其他任务继续运行。

正常路径共核对 104 组格式和 52 组容量，覆盖闰年、ISO 跨年、2038 年以后的
64 位时间数值、零容量、恰好容量、空格式、较大宽度和页面末端写入。
独立期望值位于 `probe/cases.json`；每次调用核对输出与哨兵，调用者日期数据
保持不变，格式化没有分配堆。提供者最大单函数栈帧分别为 188 和 48 字节，
不增加动态初始化。自审复核实际 ELF 的数据地址和帧成本，并拒绝 164 个失真观察样例。

以上属于模块资格。完整 V8 ELF、客体 Isolate/JavaScript、网页、视频与完整 HTML5
仍需各自的实际客体证据，不能由本模块测试代替。

## 重放

先使用完整原生入口构建同一目标，至少完成实际最终链接尝试。若后续服务仍缺失，
保留失败和缓存；不要跳过原消费者。然后在固定 Linux/WSL 构建环境执行：

```sh
python3 -B tools/verify-native-time-format.py /path/to/fresh-output \
  --integration-build /path/to/complete-native-output \
  --integer-builtins /path/to/libscalar_integer.a \
  --cxx /path/to/qualified-g++ \
  --qemu /path/to/qualified-qemu-system-i386 \
  --firmware /path/to/qualified-qemu-firmware
```

工具使用该完整目标的实际对象、源码绑定和编译命令，编译器沿用其中的固定路径。
内核编译器为已验收的 GCC 13.3.0；QEMU 的加载器/共享库需在该环境可用。
每次输出到新目录，绑定实际程序、加载器、共享库和固件。测试镜像为私有产物，
不会更新可见生产虚拟机的介质或磁盘。`acceptance.json` 记录本次证据，
不能替代新环境的真实重放。

公开工具已完成同样两次实际冷启动及独立自审，直接执行第 28 次完整目标对象；
完整链接已越过 strftime，真实下一首错为 chrono::steady_clock::now。
