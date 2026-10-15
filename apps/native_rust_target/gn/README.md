# GTOS IA32 真实 GN Rust core 规则

本模块把独立 Rust 目标接入 Chromium 的真实 `rust_rlib` 协议，并按
`//build/rust/std/rules:core` 生成的 O3/debug assertions 参数编译包内未修改的 core。
这解决 Content 的一个构建前置，完整 std 和浏览器仍未通过。

`gtos-rust-core.patch` 适用于固定 Chromium
`04bca2584cddb39e134fc7c16bb2886e02863d0e` 加已有
[GTOS 平台基础分支](https://github.com/awe0w0/chromium/tree/gtos-platform-integration)。
`patch-replay.json` 固定七个文件的前后 SHA256；私有新目录中检查并实际应用补丁后，
七个输出与本次编译候选逐字节相同。原始工具链、缓存与失败目录均保留。
这份补丁保存在 GTOS 仓库，本次未推送或修改 Chromium 平台分支。

补丁为 GTOS x86 配置自己的目标 JSON，并作为包装器输入跟踪；禁止默认宿主链接库。
`rust_rlib` 使用上游包装器的 rsp、dep-info、rlib/rmeta 与 flags 输出协议，宿主 Python
显式采用 `-I -B -S`。包装器仅增加推迟类型注解求值，以兼容已固定的 Python 3.8。
原生整数 builtins 使用已认证的 14 项归档，不冒充完整 compiler-rt 或 profiling。
Rust 能力只对 GTOS IA32 开放；x64 的原有纯 C++ 资格图保持可生成。

## 实际检查

- 新目录生成精确 core 图：5 个目标、67 个构建文件；GN 的 exec_script 全部禁止。
- 91 项源输入覆盖 core 图、必要 Python 辅助脚本及已审阅 GTOS 覆盖；实际 GN 读取
  73 项源文件和 3 项生成元数据。原始 Git、ANGLE DEPS、生成配置和包成员分别认证。
- 上游 `gen_allow_all_warnings_rsp.py` 用固定 rustc 查询生成 249 项 `-A` lint 参数；
  与真实 action 参数核对后通过隔离 Python 显式执行，不执行整个 Ninja 图。
- 实际 `rust_rlib` 的 flags、环境、513 项声明源码和输出协议逐项匹配静态审阅值，
  再以参数数组运行。源码包共 730 项、实际 dep-info 共 393 项，全部闭合并终验。
- 所有 16 个代码对象及 2 个元数据对象均为 ELF32/小端/i386/ET_REL；代码对象合计
  590,476 字节。rlib 为 66,021,998 字节，rmeta 为 65,371,912 字节，元数据节摘要一致。
- 修正 IA32 能力边界后真实重编，rlib、rmeta 和 dep-info 字节与修正前相同。
  x64 纯 C++ 图另生成 1 个目标、34 个构建文件，并核对其 ELF64 clang 工具参数，
  没有 Rust 规则。本次没有重新编译或运行 x64 对象。

`qualification.json` 保存上述结果、实际命令及输入闭合；`inputs.json` 保存来源身份。
`fixtures/` 包含生成的规则、flags、lint 参数、成员表和日志，供只读复核。大型工具链、
归档和完整上游源码不入仓。私有原始证据位于
`artifacts/native-rust-gn-core-20261015-a/build-03`。

## 执行和验收边界

构建入口使用最小环境、无网络命名空间、no_new_privs、Landlock 精确执行文件白名单，
仅新输出可写；拒绝执行其他脚本和目标产物。未从 Ninja 或日志提取命令直接运行。
实际 lint action 的解释器改用已审阅的隔离入口，其余 Rust 参数与生成规则一致。

本次不验收 alloc、完整 std、OS 线程/TLS/I/O、C ABI、Rust 客机程序或 Content。
未知平台 std 的 `no_threads` 与实际 `target_has_threads` 冲突仍需真正 GTOS 后端。
本模块验收时 Content 图因 `net:test_badgssapi` 缺 solink 失败，原始证据保持。
后续[共享链接模块](../../native_shared_link/README.md)已越过该阻塞，最新图停在
libaom 的 NASM 平台参数；完整标准库消费者与 OS 后端仍需接入。不关闭 Rust、不用空锁，
不按测试名称删除依赖。生产镜像未更新，网页、视频和 HTML5 仍未验收。
