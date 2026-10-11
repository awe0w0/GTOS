# 32 位 V8 完整原生目标集成入口

此模块将锁定版本的真实 V8、libplatform、Platform/Isolate/Context/Script 消费者和
原生入口接成 `native_embedder.elf` 可执行目标。当前配置为 GTOS IA32 的单线程、
JIT-less 诊断；关闭 Wasm、ICU 和 sandbox。完整引擎、网页、视频及 HTML5 的客体
验收尚未完成，信号处理仍不支持。

目标及阶段门槛见 [集成主线](../../docs/chromium-integration-plan.md)。本模块解决
完整目标的输入和重放入口；不能用 GN 成功、链接命令存在或某个对象编译通过判断
Isolate 已在 GTOS 运行。

## 输入与来源

`overlay/` 保存 84 个平台覆盖文件，`sdk/` 保存 426 个私有配置及 C++ 头文件，
`c-sdk/` 保存 438 个 LLVM C SDK 文件。搜索顺序固定为私有覆盖、上游 libc++、
C SDK、Clang 资源头；合并两层 SDK 会改变 libc++ 的 `include_next` 语义。
构建在独立目录
创建上游源码镜像，将覆盖及 SDK 复制到自有文件后编译；上游源码和已有输出不被
替换。SDK 来自已保留的 LLVM C 声明闭包与原生 libc++ 配置，声明不代表服务已实现。

`payload-lock.json` 固定覆盖、SDK 和许可证内容。`source-lock.json` 固定构建工具、
327 个 Clang 资源头、40 个 Linux IA32 宿主库文件、宿主 Python 及以下源码版本：

| 输入 | revision |
| --- | --- |
| Chromium DEPS 对照 | 04bca2584cddb39e134fc7c16bb2886e02863d0e |
| V8 | be042d4462bee463c9b785701b8c6ee4576ee0a3 |
| build | 22711c1cb2ea5fc0f80f709aa2ee9d8ce80b5703 |
| Abseil | 838f15bf6f6fb0d7a3dc37e35f355d9673275138 |
| libc++ | 97b436da4c33663581d394f4ee0a5977fc38c2f4 |
| libc++abi | 09351f6ec00c2b65f2d03585ae6da07098b024dc |
| LLVM libc | ebe33e01982dbbf879661e3b6b78450f3020a53f |
| zlib | 456ae730017b9b4bd3371927abc82627fd51feef，与 V8 DEPS 一致 |

需要事先准备该 V8 revision 对应的 DEPS 依赖、Linux x64/IA32 sysroot、锁定的宿主
Python 和官方 Clang/GN/Ninja。工具核对源码 revision 和已跟踪文件修改；sysroot
仍是准备好的上游检出输入，未作为 GTOS 文件再分发。Linux IA32 宿主运行库只运行
torque 等构建工具，GTOS 目标不链接宿主 libc。此处不提交完整上游 checkout。

## 构建与失败重放

在 Linux 或 WSL 中，从 GTOS 仓库执行：

```sh
python3 -B tools/build-native-v8-integration.py /path/to/fresh-output \
  --v8-source /path/to/pinned-v8-checkout \
  --clang /path/to/pinned-clang/bin/clang \
  --gn /path/to/pinned-gn \
  --ninja /path/to/pinned-ninja \
  --host-i386-runtime /path/to/pinned-linux-i386-runtime/root \
  --integer-builtins /path/to/pinned-libscalar_integer.a \
  --enable-diagnostic --jobs 8
```

输出目录必须不存在；工具保留所有失败、超时和缓存，不提供清理或覆盖选项。
当前 GN shell 工具路径要求没有空格或 shell 元字符。工具、SDK 和运行库通过内容
哈希绑定，无需旧私有诊断目录。`--prepare-only` 只执行 GN 图、完整依赖及实际链接
命令检查；默认执行完整 Ninja。超时用 `--build-timeout` 指定，默认为 7200 秒。

输出包括 `requested-args.gn`、完整 GN 依赖、全部规则的编译数据库、真实 executable
命令、Ninja 原始日志和 `status.json`。失败的 Ninja 返回非零；应读取实际失败位置。
`driver_failure` 表示输入、准备或驱动错误，不能记成 OS 根因。超时的命令退出码为
124。超时只结束该命令创建的会话及其中的编译器进程组。

`--cache-from /path/to/retained-output/build` 将已有缓存复制到新输出后续编；原缓存
不被修改。该缓存必须保留相邻的 `module-input-snapshot/source-lock.json`，并与
当前源码版本、工具、资源头及宿主运行库一致。续编仍重新生成完整 GN 目标，保留
新的命令、输入与日志；`status.json` 中 `cold_build=false`，不能计作冷构建验收。

为保留今后续编的输入，在构建结束且源码锁未改变时，将此模块复制到输出的
`module-input-snapshot/`，并另存构建脚本原副本。尚未保存输入快照的旧输出不能
直接用于该续编选项；其缓存和失败日志仍可保留。

## 实际重放结果

2026-10-11，分层 SDK 的全新输出冷构建与缓存续编均到达
`absl/log/internal/log_message.cc:59` 的 `unistd.h` 缺失，消费者为日志
`FailQuietly` 的 `_exit(1)`。完整 GN 依赖清单相同，输入在构建期间未变。
完整 ELF 尚未生成，Isolate、JavaScript、网页、视频及 HTML5 客体验收仍未通过。
最新精确命令、诊断及历史失败见 [机器清单](../../docs/chromium-integration-status.json)。

初版合并 SDK 改变了 libc++ 包装头搜索顺序，产生的编译失败及输入版本另行保留。
公开驱动在冷构建版本上只增加已实际验证的宿主 `clang++` 内容检查；运行库及
编译/链接命令保持相同。没有用配置错误、超时或单个对象通过代替原生运行证明。

## 平台能力边界

GTOS 保留独立平台身份和 ELF32 格式身份。动态 ELF 符号/VDSO 路径不适用于目前
静态装载模型，平台覆盖将其排除；Linux IA32 对照仍保留原有动态 ELF 能力。

Abseil 日志立即退出接到 GTOS Exit ABI；GTOS 日志 ID 使用 uint32，与实际任务 ID
保持一致。[退出消费者验证](../native_v8_exit_probe/README.md)在 CPL3 验证实际
FailQuietly、ExitQuietly、NullStreamFatal 跳过析构和缓冲刷新，以及正常返回的
原生入口清理、同伴存活和精确回收。信号安装明确报错并触发用户故障；这不提供
信号处理、pthread、sigaction、altstack 或 alarm。其余未调用桥及完整日志格式化
仍待验收，完整 Isolate 资格状态保持未通过。

熵分支连接 CPUID/RDRAND 的真实 CPL3 源，检测能力、有限重试并检查地址与长度。
[实际熵消费者验证](../native_v8_seed_probe/README.md)通过 O0/O2 的源开/关矩阵、
用户页故障和精确回收；O2 对象与完整目标逐字一致。完整目标已越过该对象，当时
首个失败是 futex.h:31 的同步依赖。该证明不包括真机熵质量、熵池或 TLS。

随后修复 futex.h/mutex.cc 的未用 POSIX 头条件，portable-resume-07 的同图完整续编
已越过这两个对象，首个失败前进到 zlib/zconf.h:505。实际 waiter 预处理选择标准
C++ 分支；GTOS pthread 排除条件原已存在，此前相反的分析已更正并保留。条件
等待目前只有 SDK 声明，原生提供者尚缺；本轮不新增等待或线程运行证明。

随后为 GTOS 选择固定 zlib 的完整标量编解码分支，gzip 文件操作连接已有 File ABI。
zlib-draft-resume-11 的真实完整目标已越过这些对象，当前首个失败为 cctz 的
time_zone_libc.cc:121；同轮还缺少时区文件接口。完整 ELF 尚未生成。
[zlib 消费者验证](probe/zlib/README.md)已从公开入口完成冷构建和缓存重放，验证
gzip 解码、截断 EOF、追加继续解码、错误及资源回收；O2 的 16 个 zlib 对象与完整
目标逐字一致。实际压缩初始化在现有堆容量下返回 Z_MEM_ERROR，回滚通过；压缩
成功仍未验收。缓存对象及其自报哈希同时被篡改时，由独立资格锁准确拒绝。

每轮从完整目标的新失败取得消费者证据，再实现必要服务和客体边界回归。已有
C FILE、VM、TLS、时钟及 PNG 桌面成果继续复用。保持内核容量和整数 ISA 门禁，
使用实际完整 ELF/映射/堆/栈需求决定后续改动；8364 字节 long-double 格式化帧
问题仍未解决。生产 ISO 和可见虚拟机磁盘不由本工具更改。

V8/Chromium 与 Abseil 覆盖保留原版权头，SDK 保留 LLVM 版权头；对应许可证随
模块保存。来源锁描述构建输入，不构成完整运行库或浏览器资格证明。
