# GTOS IA32 Rust 目标与真实 core 编译资格

真实 Chromium Content Shell 在 `build/config/rust.gni` 中首先因 GTOS 没有 Rust
ABI 映射而失败。本模块固定编译器及其自定义目标规格，并编译该工具链内未修改的
真实 `core` 标准库，为后续 Content/Rust 接入提供可核对的基础。

## 目标与来源

- Chromium：`04bca2584cddb39e134fc7c16bb2886e02863d0e`。
- 包内 rustc：`1.100.0-nightly`，提交 `1edd55dcfcd573872c727fa3e086369a71661ee0`，
  Chromium 工具链版本 `1120-llvmorg-24-init-7747-g62397f8b`。
- 已缓存官方 Rust 工具链包的 SHA256 为
  `b13f3ee4862d1dca5e3d4d7368f0f8b3bf5033a98a9c146e75f73fb0a4741dd2`，
  与固定 Chromium Git 提交的 DEPS 核对；更新脚本仅供阅读，没有执行。
- `i686-unknown-gtos.json` 明确使用 `os=gtos`、32 位 x86、静态 ELF、abort、SSE2，
  LLVM 架构 triple 为 `i686-unknown-none-elf`，不声明 Unix/Linux 或硬件 TLS。
  保留多线程目标属性，避免未知平台静默选用无线程锁。此属性本身不证明线程可用。

自定义目标 JSON 与 schema 随编译器固定；实际 `--print cfg` 和规范化规格已通过。
架构布局参考同一编译器的内建 IA32 目标，但 GTOS 始终使用自己的目标文件。

## 检查方式

先按已认证归档核对 `core`、`stdarch` 和 `portable-simd` 三棵源码树的 730 个文件。
使用固定参数直接调用真实 rustc，在空 sysroot 中生成 rlib、rmeta 和 dep-info，
不执行 Cargo、build.rs 或目标产物。参数与环境见 `core-compile.json`；这是独立的
编译器资格命令，尚不是完整 GN 图生成出的编译行。

首次编译返回 0、无标准错误，但旧归档检查器错误假定 rmeta 是非 ELF 文件，因此
该次整体未通过。读取同包编译器源码后确认，`lib.rmeta` 和 `lib.rmeta-link` 都是
目标 ELF 包装对象。新的独立复核保留原失败及缓存，重新绑定全部来源和实际依赖，
流式解析全部代码/元数据对象，并核对 `.rmeta` 节与独立 rmeta 文件的 SHA256。
只有输入终验和归档检查一起通过，才在 `acceptance.json` 中置位。

实际闭合 393 个依赖，来源文件共 42,757,995 字节。归档包含 16 个 ELF32/i386
代码对象（共 434,836 字节）和 2 个同架构元数据对象；rlib 为 65,962,704 字节，
独立 rmeta 为 65,466,725 字节。大部分体积属于元数据，不是客机驻留内存需求。

源码锁、代码/元数据成员及实际编译参数均在本目录；大型工具链、归档和巨型上游
checkout 不加入仓库。工作区原始材料位于 `artifacts/native-rust-gtos-20261015-a`。
已有生产镜像、V8、内核和运行库实现未改。

## 当前 Content 首错与边界

私有真实 Content Shell 图已越过 Rust ABI 身份、原生整数 builtins、崩溃键后端选择、
固定链接并发数、构建时间戳和通用测试资源的 GTOS 分支。最新原始失败在
`base/test:immediate_crash_test_helper`：GTOS 工具链尚无 `solink`。只读诊断的
驱动退出码 0 表示错误采集完成，实际 GN 返回 1，完整图仍未通过。

GN 仅允许固定时间戳脚本及 `default` 参数；其来源与输入经过固定 Git 核验，
由 Python `-I -B -S` 入口执行。其余脚本保持禁止。所有构建/复核均使用最小环境、
无网络命名空间、no_new_privs、精确执行文件白名单及新输出目录写入限制。

本模块不验收 `alloc`、完整 `std`、线程/同步/I/O 后端、C 互调用、客机 Rust 程序、
完整 Content 构建或浏览器。当前 std 对未知平台选择的 `no_threads` 与
`target_has_threads` 存在明确冲突，仍需真实 GTOS 后端。下一步继续同一 Content 图，
厘清共享库辅助目标的实际需求与静态工具链边界，再按真实标准库消费者接入 OS 服务。
本地 HTML/CSS/JS 首帧、DOM、网络、视频和 HTML5 仍未验收。
