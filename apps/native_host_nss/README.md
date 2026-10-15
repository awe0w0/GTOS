# Linux 宿主 NSS 构建配置

本模块解决 GTOS Content 构建准备中的一个宿主依赖：为 Linux x86/x64 次级工具链
选择固定 Debian sysroot，并使用真实离线查询结果提供 NSS/NSPR 的 GN 配置。
GTOS 主工具链继续使用 `use_sysroot=false`，原有 freestanding 断言保留。

两文件补丁为 `gtos-host-nss.patch`：Linux 工具链只在 `target_os == "gtos"` 时设置
次级 `use_sysroot=true`；元数据缓存保留上游所有条目，追加当前私有源/输出布局的
两个 NSS 条目。布局为 `OUT/source` 与 `OUT/build`，不是任意目录布局的通用缓存。
补丁已在新目录应用，并与实际验收候选逐字节核对。

## 已验收

- 固定 Chromium 提交 `04bca2584cddb39e134fc7c16bb2886e02863d0e` 的 sysroot 声明
  与两个已缓存归档摘要一致。每个归档内的 NSS/NSPR `.pc` 和两个头文件共四个选定
  成员，与安装副本摘要一致；没有宣称整个安装树均已验收。
- 固定 `pkg-config` 与六个 ELF 依赖完成只读登记。双架构离线元数据查询返回 0，
  两个 include 目录、六个库和空额外参数均与审阅结果一致。NSS 的 `ssl3` 由上游
  既有规则过滤，Chromium 自身 TLS 配置未变。
- 真实 GN 小目标生成 5 个目标、43 个 GN 文件。实际依赖清单有 48 项，已完整绑定；
  111 个源输入和全部绑定输入终验通过。
- 两次真实 `gn desc` 分别检查 Linux `clang_x86`、`clang_x64`。C/C++ 的两个
  `-isystem` 路径、`nspr4/nss3/nssutil3/plc4/plds4/smime3` 六个库及 NSS config
  引用均一致。生成与描述使用同一根路径、点文件、根目标和 root-pattern。

有效记录为 `qualification.json`，对应私有 `graph-04`。`graph-01` 到 `graph-03`
的失败均保留，原因见 `self-review.md`；不能把其中的 GN 返回 0 当作整体通过。

## 范围

本模块验收宿主构建元数据和 GN 配置。夹具 C 文件未编译，没有运行 NSS、TLS、
GTOS 程序或浏览器，也未验收信任库、完整 Rust std、libxml 线程/fd/ICU 后端。

继续使用同一 Content 目标的实测图已越过 NSS、PipeWire/GIO 和 D-Bus 首错，当前
首错是 Linux `device/udev_linux/BUILD.gn` 的受限元数据探测。该次驱动完成诊断，
实际 GN 仍返回 1；没有 Content 对象。后续从宿主 udev 启用条件继续，再推进
GTOS Content 本地首帧、DOM、网络、视频和 HTML5。主目标没有缩小。

## 证据与重放

`source-lock.json`、`inputs.json`、`query/` 和 `fixtures/` 保留来源、实际结果及参数。
固定工作区的验收入口为：

`F:/GTOS-Chromium/artifacts/native-host-nss-20261016-a/launch-graph-v4.py`

上游元数据查询与归档审计入口位于
`F:/GTOS-Chromium/artifacts/native-rust-bin-20261015-a/`，分别为
`launch-nss-query-v1.py` 和 `launch-sysroot-audit-v1.py`。仓库脚本是这些已执行版本的
源码快照；重放依赖已固定缓存，不能直接改路径运行或宣称空白机器 bootstrap 通过。
旧输出不覆盖，入口不接受任意工具、路径或隔离覆盖参数。

执行沿用[本地执行栅栏](../../docs/native-command-execution.md)：固定工具摘要、
Python `-I -B -S`、最小环境、独立无网络命名空间、no_new_privs、Landlock 精确
执行白名单、源码只读和新输出目录写入。独立查询只增加已锁定的 `pkg-config`
执行文件；NSS GN 探针禁止全部 helper，Content 图仍只允许原来三处精确 helper。

生产镜像未改。GTOS 原生浏览器、网页、视频与 HTML5 尚未完成。
