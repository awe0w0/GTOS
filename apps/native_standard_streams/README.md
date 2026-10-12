# GTOS 原生诊断标准 FILE

本模块验证完整 V8 的真实 fflush/标准流消费者所需的 FILE 接线。提供者位于
`apps/native_v8_integration/overlay/tools/gtos_isolate_diagnostic/native-c-file/platform_file.cpp`，
使用实际完整目标生成的对象；普通字节流核心复用已验收的固定 LLVM FILE 算法。

stdout/stderr 输出经原生 GTOS_SYS_WRITE 分块提交。控制台输入和 fileno 缺少
原生服务，明确返回 ENOSYS；控制台 seek 返回 ESPIPE。标准对象常量初始化，
无初始堆分配，公共 C 与 LLVM 内部变量指向同一份指针存储。freopen 可绑定真实
文件，返回原生正值句柄，不假设 POSIX 保留编号；fclose 释放缓冲、关闭句柄并
移除标准指针，静态 FILE 的映像页面在进程 Reap 时回收。

两次 QEMU8 冷启动为 32 MiB/1 CPU 与 64 MiB/4 CPU，均在当前 BSP 单用户线程
契约下执行。12 个任务完成 552 项标准流检查及受影响的普通文件回归，20 个
异常退出时保留的句柄全部回收；正常退出检查堆最后分配释放、旧句柄失效和堆
地址不可读，故障/取消路径检查 17 帧堆保留至安全 Reap。独立未修改的 LittleFS
读取核对实际重定向内容；反例检查拒绝缺失/重复输出、错误计数及回收记录。

## 重放

先按[完整目标入口](../native_v8_integration/README.md)构建到真实 GN/对象与最终
链接阶段。完整引擎可仍因下一缺失服务失败；这不应跳过任何原消费者。再执行：

```sh
python3 -B tools/verify-native-standard-streams.py /path/to/fresh-guest-output \
  --integration-build /path/to/complete-native-output \
  --emulator-runtime /path/to/qualified-qemu8-bundle \
  --clang /path/to/pinned-clang/bin/clang \
  --libcxx-source /path/to/pinned-libcxx \
  --integer-builtins /path/to/libscalar_integer.a \
  --cxx /path/to/g++ --cc /path/to/gcc
```

QEMU8 包含 bin 包装入口、root 中的真实程序/加载器/库、firmware 与相应内容清单；
工具绑定实际可执行文件及固件。沿用经过验收的 instruction-counted ATA 时序。
每次使用新输出，保留全部失败。原生代码、旧缓存和可见生产虚拟机不会被该验证
替换。`acceptance.json` 记录已完成资格及范围，不能替代新的实际重放。

本模块没有实现控制台键盘输入、通用 POSIX 描述符表或原生多线程。完整 V8 ELF、
客体 Isolate/JS、浏览器、视频和 HTML5 尚未通过；所有原容量上限保持不变。
