# GTOS IA32 Rust 可执行链接

真实 Content 图在 BoringSSL 的 Rust 测试定义处要求 `rust_bin` 工具。GN 会在最终
root-pattern 筛选前验证这些定义。本模块为 GTOS x86 增加真正的 Rust 可执行链接，
保留测试和真实依赖；完整 Content 图已越过该错误，当前停在 Linux 次级工具链的
`printing/BUILD.gn:482` CUPS 配置探测。浏览器、视频与 HTML5 仍未运行。

## 规则与实际验证

`gtos-rust-bin.patch` 基于已有 [GN core](../gn/README.md) 与
[共享链接](../../native_shared_link/README.md) 覆盖，应用于 Chromium 根目录的
`build/toolchain/gtos/BUILD.gn`。补丁在新目录实际应用并逐字节核对，见 `patch-replay.json`。
它使用目标 JSON 规定的 GNU LLD 接口，传递真实响应文件、依赖、extern、链接参数和
环境，输出 `.elf`、depfile 与 Rust 参数记录。原有八种 C/C++、汇编、归档、共享链接及
Rust rlib 规则与已验收版本相同。新增工具只在 IA32 Rust 分支内，未增加 x64 Rust 能力。

真实 GN 生成两个资格目标、读取 34 个文件，102 项锁定源输入覆盖完整资格图。
Rust `no_std` 程序读取 C 的运行时数据、执行 64 位除法，再调用 C 函数；实际 Clang、
Rustc、LLD 生成 2076 字节的静态 ELF32/i386。检查入口、段/节/符号边界、没有动态
解释器/宿主库/TLS、无可写可执行段、不可执行栈和无残留重定位。Rust 到 C 的分支、
数据地址以及真实本地隐藏函数 `__udivdi3` 均已解析。缺失 C 提供者时实际链接失败，
报告三个未定义符号且无 ELF 输出。最终入口完整重放通过，产物字节与前次相同。

资格目标显式复用已验收的真实 core 和本轮真实 compiler_builtins，隔离工具规则。
它未链接完整 std，也未执行目标程序或验收运行时 C ABI、除法数值、退出/回收。
真实 Content 图继续保留原有标准库依赖，不能以这两个资格目标代替完整构建。

## compiler_builtins 来源与边界

首次真实链接缺少 `compiler_builtins` crate。C 整数归档不能替代其 Rust 元数据；
`external-clangrt` 仅控制 sanitizer 运行库，见 [Rust 官方说明](https://doc.rust-lang.org/nightly/unstable-book/compiler-flags/external-clangrt.html)。
本轮从已认证 Rust 包的 395 个成员编译未修改的完整 compiler_builtins/libm 源码，
复用真实 core，保留上游 arch/compiler-builtins/default/unmangled-names 特性及目标的
f16/f128、SSE2 配置。配置由锁定 `build.rs`、`configure.rs` 和实际 target cfg 静态推导；
**没有执行 build script，也不是上游 compiler_builtins GN 规则的完整重放**。

实际 Rustc 返回 0，stdout/stderr 均为空。归档为 2,902,920 字节，独立 rmeta 为
2,130,346 字节；全部成员外层为 ELF32/i386/ET_REL，包含 384 个代码对象、1 个数据
对象、1 个空单元和 2 个元数据对象，181 项实际依赖闭合。嵌入 rmeta 与独立文件
逐字节对应。空单元只有空文本节、注释、不可执行栈标记和本地文件符号，单独检查，
不计为代码。`compiler-builtins/` 保存输入、实际编译记录及只读复核结果。

原 core 检查器假设所有 CGU 都有代码，导致首次整体检查失败；之后仅只读复核缓存，
未重复编译。早期 Rust ELF 检查也曾误把隐藏内建函数当成缺失导出，相关失败保留。
最终资格入口已修正符号分类并完整通过；不将历史失败记录改写为成功。

## Content 候选与下一步

`content-candidates/` 保存真实图中使用的四个候选文件：通用菜单模型允许 GTOS 求值，
libxml 选择独立 GTOS 配置。菜单异步关闭依赖任务循环，源码逻辑未改；libxml 保留
线程、ICU、HTML 和 XPath，未借用 Linux OS 宏。原始 libxml chvalid/parser 已分别
生成 4148/109148 字节的 IA32 对象，但 threads.c 缺 `pthread.h`、xmlIO.c 缺 `fcntl.h`。
这些是已复现的接口缺口，**候选配置及完整 libxml 运行时尚未验收**。

Content 使用精确 root-target/root-pattern，保留传递依赖。时间戳、品牌、版本只允许
锁定脚本和精确参数；真实附加 eval/文件输出选项被拒绝。显式 `use_glib=false` 用于
Linux 宿主命令行工具，GTOS 默认也不使用 GLib。CUPS helper 仍被拒绝，尚未执行。
接下来审查该宿主打印探测，再继续完整 std、libxml 线程/fd/ICU 与 Content 首帧、DOM。

`qualification.json`、`content-graph.json`、`libxml-compile.json` 区分已验收工具能力、
诊断和候选。`fixtures/` 保存真实 GN 规则、源码、参数及关键日志。
`graph-v4.py`、`qualify-v3.py` 及入口脚本固定引用
`/mnt/f/GTOS-Chromium/artifacts/native-rust-bin-20261015-a`，复现需要现有 pinned
工作区、前序覆盖、工具和缓存；拒绝覆盖旧目录，未提供空白主机 bootstrap 或下载入口。

执行遵循[本地栅栏](../../../docs/native-command-execution.md)：最小环境、Python
`-I -B -S`、无网络、no_new_privs、Landlock 精确执行文件白名单，只有新输出可写，
目标 ELF 禁止执行。生产镜像未变，原生 V8 客机基线保留。
