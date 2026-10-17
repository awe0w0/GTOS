# 原生 BoringSSL 线程与 MD5

真实 Content/V8 `::base` 扩展根依赖 MD5。原始 BoringSSL 在 GTOS 的完整依赖
预处理进入 pthread 头，原有汇编规则又未传递目标 include/define。本模块为
GTOS 接入原生读写锁、once 和五槽线程存储，补齐汇编参数及完整 MD 依赖，
保留真实 MD5 C++ 和 IA32 汇编实现。`OPENSSL_THREADS` 与汇编均保持启用。

## 接入方式与边界

`boringssl-native.patch` 只改固定上游的 GTOS 平台选择、锁存储和条件源成员；
新增实现位于 `overlay/third_party/boringssl/src/crypto/thread_gtos.cc`。
`gtos-asm.patch` 只改实际 Content 工具链的 asm 规则，其他规则字节不变。
原文件、补丁结果、上游提交及对象摘要见 `source-lock.json`。补丁已在独立
副本应用，并确认三个结果与实际编译候选逐字节相同。

这些增量用于后续 Content 源视图集成。现有完整 V8 工作负载的冻结覆盖层与
锁文件保持原版。当前不是完整 BoringSSL 归档或空白主机冷构建验收，未更改
生产镜像。BoringSSL 补丁和新增实现使用所附 Apache-2.0 许可，Chromium
构建补丁的许可另见 `CHROMIUM-LICENSE.txt`。

锁以真实原生 tid 记录所有权；四个 BSP 上下文共享地址空间，新读者等待已
排队写者，已有读者允许读重入，写者按无符号票号顺序推进。错误线程解锁、
读写升级、写递归和带持有者销毁触发原有 CHECK。once 在回调完成后发布；
同一 once 的递归调用失败，独立 once 可以嵌套。

TLS get 不分配。首次 set 先预留记录，在表锁外注册真实 `__cxa_thread_atexit`，
成功才发布值；失败则回滚并按上游契约调用析构函数。退出时先快照、清空并
释放记录，再在锁外析构；析构重入产生的新登记由已有运行库退出循环处理。
同一槽的调用者仍须遵守上游“同一析构函数、每线程设置一次”的契约。

## 验收证据

正式 Content GN 生成通过，BoringSSL 所有者仅增加原生成员。真实 C++23
GN 参数完成两份完整 C++ 源的 M/MD 来源核对；原始 MD5 汇编也完成完整
依赖检查。C++ 可执行节及汇编全部按原始字节反汇编审查，未引入硬件 TLS
或初始化数组。这里只验收本次三个提供者对象，未宣称其余库成员已编译。

`probe-thread/` 使用 C++23 适配器持有实际库类型，旧 C++20 V8 Thread
消费者只经平坦 C ABI 调用。默认单上下文与显式四上下文均在 GTOS CPL3
运行，两种配置共 122 个正向上下文、66 次正常 Join，覆盖：

- 同时读者、已有读者重入、写者优先于新读者、FIFO，以及一个持锁写者加
  三个排队写者的最大边界；票号从 `0xfffffffe` 跨零到 `2`。
- once 的并发发布、递归拒绝和独立嵌套；错误所有权、升级、带锁销毁、非法
  TLS 索引和空初始化回调均产生预期原生故障退出。
- 五槽线程隔离、八轮线程复用、空值析构、快照分离与析构重入。真实堆耗尽
  使首次退出登记失败，值恰好析构一次、槽位回滚，释放堆后可成功重试。
- 整组取消、真实 emutls OOM 和页故障；物理页、VM、栈、任务及 FP 所有者
  精确恢复基线。错误 once 与进程全局共享 TLS 两个对照均被拒绝。

`probe-md5/` 调用真实 MD5 和未改动的汇编，在两种配置完成 6560 次有效调用、
8 个正向 CPL3 上下文。七个 RFC1321 向量及十六种二进制长度覆盖16种对齐、
零长输入、十种分块长度、只读页和紧邻保护页的合法读写，比较完整16字节
摘要、哨兵及 cdecl 保留寄存器。越界读、越界写及只读输出分别产生精确预期
页故障；错误摘要和破坏 EBX 的对照均被拒绝。

测试配置为 32MiB/1CPU、64MiB/4CPU，用户线程仍仅在 BSP 调度。未证明用户
SMP、单独强杀持锁 worker、完整调用链栈界、当前大映像装载 ABI，或真实
扩展根入口的 AtExit/TLS 生命周期。`fixture-helpers.h` 是实际小型内核观察器
辅助源码；四份历史 ABI 头复用 `../native_sdk_math/fixture-abi/`，不覆盖当前
系统头，也不能代替当前装载器验证。两个负面对照不构成资源回收证明。

原始日志和结构化结果见 `logs/`、`acceptance.json`。公开源仅按摘要复制，
没有重跑一套公开构建器或远端 CI。执行使用最小环境、Python -I -B -S、无
网络命名空间和 Landlock 精确工具/新输出白名单，遵守[本地命令栅栏](../../docs/native-command-execution.md)。

## 后续路线

完整扩展根保留原入口、main 和旧输入顺序后，未定义符号从14降为13，仅移除
MD5；剩余属于 PersistentMemoryAllocator / DelayedPersistentAllocation，
该根仍没有 ELF。下一步处理真实私有堆、VM reserve/commit/release、文件映射
和 flush，再验实际入口生命周期、扩展 Isolate、Content 首帧/DOM/输入。
已验收的[原生 V8 JS 基线](../native_v8_launch/README.md)继续成立；网页、视频
及 HTML5 尚未验收。路线依据 [Issue #9](https://github.com/awe0w0/GTOS/issues/9)。
