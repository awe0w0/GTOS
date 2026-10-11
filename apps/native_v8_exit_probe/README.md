# 真实 Abseil 日志退出的 GTOS 客体验证

此模块对应完整 `native_embedder` 的实际阻塞：`log_message.cc` 需要立即退出，
且 GTOS 无符号任务 ID 与原 Abseil 日志 ID 类型不一致。类型适配后完整目标已
前进到 `seed_material.cc:17` 的熵读取依赖。这里验证实际 Abseil 退出消费者、
原生入口和内核回收；完整 V8 Isolate、JavaScript 与浏览器仍待验收。

测试从已结束且冻结输入的完整目标读取真实源码和编译命令，保留所有构建依赖。
使用真正的 `LogMessage::FailQuietly`、`ExitQuietly`、`NullStreamFatal` 和
`InstallFailureSignalHandler`；没有替换这些函数。原生入口使用完整目标的
`native-start.s`、`native-entry.cc` 和 TLS 析构实现。测试消费者替换脚本入口，
因此运行结果不作为 V8 引擎启动证明。FILE 和内核基础对象只在源码锁、工具和
对象 SHA256 全部匹配时复用，新的内核仍经过原有整数指令审计。

## 行为与验收

每组六个独立 CPL3 ELF：正常返回、实际日志立即退出、两条内联立即退出、
信号安装拒绝故障和持续运行的同伴。每个程序有两个真实 FILE 句柄、64 KiB
VM 堆及独立 CR3。同伴在其余五个程序停止和回收时保持运行。

- 正常返回 17：自动析构、TLS 析构和 fini 各执行一次，TLS 回调刷新 16 字节缓冲。
- 三条立即退出返回 1：跳过上述析构及用户缓冲刷新。
- 信号安装明确输出“不支持”并产生用户 #UD；这不提供信号处理能力。
- 同伴最终取消返回 73；每次停止后的句柄、帧和 FP 所有权都在 Reap 精确回收。
- 正常入口执行构造/析构数组；立即退出与故障保留入口阶段 2 和未返回结果。
- 已刷新文件仍为 9 字节；未刷新缓冲在立即退出、故障和取消后为零字节。
- 用户同伴、ring0 同伴和启动任务继续推进；测试盘两端的八个保护扇区保持原字节。

O0/32 MiB/单 vCPU 与 O2/64 MiB/四 vCPU 使用 QEMU 8 指令计数配置；GTOS 用户
调度仍只在 BSP 执行。观察器另行拒绝缺失或重复记录、错误退出/析构/帧计数和
同伴停滞。不会因此扩大现有进程、页、区域、堆或栈上限。

## 重放

需要准备好的完整目标输出及其 `module-input-snapshot`，以及通过资格验收的
`native_c_file` 构建输出。此前失败和快照应保留；输出必须是新目录。
完整目标入口不会自动保存模块快照，保存步骤见
[完整构建模块](../native_v8_integration/README.md)。

```bash
export PATH=/path/to/qualified-qemu8/bin:$PATH
export GTOS_QEMU_DATA_DIR=/path/to/qualified-qemu8/firmware
python3 tools/build-native-v8-exit-probe.py /new/output \
  --complete-build /path/to/completed-native-embedder-build \
  --qualified-file-build /path/to/qualified-native-c-file-build \
  --clang /path/to/pinned/clang \
  --cxx /path/to/qualified-gcc13/g++ \
  --integer-builtins /path/to/libscalar_integer.a
```

每次运行新建带保护区的 2064 扇区测试盘并显式格式化中间卷。工具不接受现有
应用磁盘路径；测试 ISO 使用独立无界面的 QEMU，生产可见 VM 不加载它。
源码/依赖快照、编译与链接日志、实际 ELF 符号、整数指令审计和客体证据保存在
输出目录。已有内核对象的字节复用依赖对应的已验收构建输出；此入口不负责
空白主机 bootstrap，不提交巨型上游源码或构建缓存。
