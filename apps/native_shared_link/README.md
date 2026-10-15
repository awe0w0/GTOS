# GTOS IA32 共享链接规则

为 Chromium 的 GTOS x86 工具链补充真正的 `solink`、`solink_module` 和 PIC 配置，
解决 Content 构建图遇到共享库目标时没有链接工具的问题。模块限定为构建能力，
GTOS 动态加载器、完整 Content、网页、视频和 HTML5 尚未验收。

`gtos-shared-link.patch` 的基线是 Chromium
`04bca2584cddb39e134fc7c16bb2886e02863d0e`、已有 GTOS 平台基础覆盖及
[真实 GN Rust core 补丁](../native_rust_target/gn/README.md)。三文件的前后 SHA256
记录在 `patch-replay.json`，在全新目录实际应用后与本次资格候选逐字节相同。
补丁随 GTOS 仓库交付，本次未修改 Chromium 远程平台分支。

## 实现与检查

- IA32 的共享库和模块使用锁定 LLD 的 ELF i386 shared 模式、响应文件、SONAME，
  严格拒绝未定义符号和文本重定位，设置不可执行栈。GN 正确传递依赖共享库、`-L` 和 `-l`。
- 共享目标显式使用 `-fPIC`，覆盖工具链早先的非 PIC 默认值。PIC 配置的定义与引用均
  限定 IA32，避免 GN 加载配置文件时使 x64 纯 C++ 目标失败。
- C/C++ 依赖输出改为 `-MD`，包含系统头；原有静态链接、汇编、归档及 Rust 规则保持。
- 真实 GN 生成 4 个资格目标、读取 40 个构建文件；96 项锁定源输入覆盖该图与必要
  配置。逐项核对生成规则、参数、依赖及输出后，以固定参数数组运行 Clang/LLD。
- 三个真实 ELF32/小端/i386/ET_DYN 分别为 1904、1888、1888 字节。检查段/节边界、
  无 PT_INTERP、无可写可执行 LOAD、不可执行栈、无 TEXTREL/RPATH/RUNPATH，
  精确 SONAME、NEEDED、导入导出及 GOT/PLT 重定位。消费者仅依赖本次产生的共享库。
- 真实非 PIC 对象被 LLD 以 R_386_32 拒绝，缺失提供者时以未定义 GtosSharedAdd 拒绝，
  两次均返回非零且没有共享输出。正常命令全部成功，终验所有输入与别名未变。
- 修正架构边界后重放完整资格流程，三个 IA32 ELF 字节均保持一致；x64 纯 C++ 图另行
  生成 1 个目标、34 个构建文件。没有给 x64 添加共享或 Rust 能力，也未重编 x64 对象。

`fixtures/` 保存资格用 C 源、BUILD.gn、生成规则、参数及关键日志。这里的三个独立目标
显式关闭默认运行库依赖，只验证链接工具；真实 Content 图保留其原有运行库依赖。
`qualification.json`、`inputs.json` 保存实际参数、产物摘要及来源；
`elf-shared-check.py` 是本次使用的只读结构检查器。原始证据目录为
`artifacts/native-shared-link-20261015-a/qualify-02`。

## Content 路线与限制

同一候选接回真实 `//content/shell:content_shell`，保留传递依赖并使用精确 root-pattern。
图已越过原先的 solink 错误，最新停在 `third_party/nasm/nasm_assemble.gni:142`：
GTOS 没有定义 `_nasm_flags`，由 `libaom_nasm` 触发。GN 仍返回 1，尚无完整依赖闭合
或 Content 对象构建；诊断驱动成功收集错误不代表 GN 成功。

本地首帧准备显式关闭可选 Kerberos 和 Google 官方内部 API 密钥。时间戳与品牌信息
分别执行锁定的上游脚本和精确参数；品牌调用拒绝 eval、输出文件及额外选项。
内部密钥探测脚本未执行，没有读取密钥文件。证据见 `content-graph.json` 和相关日志。

下一步处理真实 IA32 NASM 格式和 libaom 平台配置，再继续 Rust std 的 GTOS 后端、
Content 首帧与 DOM。不能用 Linux 宏、空线程实现或删除测试目标代替平台适配。
本次没有验收真实 base 辅助共享库、动态加载、完整 C++/Rust 共享运行库或视频解码。
生产镜像未变；既有原生 V8 客机验收保留。

执行遵循[本地栅栏](../../docs/native-command-execution.md)：最小环境、隔离 Python、
无网络命名空间、no_new_privs、Landlock 精确执行文件白名单；仅新输出可写，目标
产物禁止执行。不执行从 Ninja 或日志提取的命令，所有旧失败和构建缓存保留。
