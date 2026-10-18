# 原生 Blink mojom_platform 生成链

本模块补齐真实 `content/shell/app/shell_main_delegate.cc` 在
`content/public/browser/render_frame_host.h:41` 处缺失的
`third_party/blink/public/mojom/browser_interface_broker.mojom-forward.h`
所属的原生 Mojo 生成链。

达到的层级必须分开读：

| 层级 | 状态 |
|---|---|
| 产物生成 | 已通过：2976 个真实生成物，目标头在内 |
| 装入 consumer 视图 | 已通过：2976 个产物以只读别名进入 `build/gen` |
| M 实际读取该头 | 已越过：M 不再在 `:41` 报错 |
| M 通过 | 未通过：M 停在 `:45` 的另一个 owner |
| CXX / MD / ELF32 | 未执行：M 未成功，门槛未解锁 |
| 浏览器 / 网页 / 视频 / HTML5 | 未运行 |

## 缺口与 owner

缺失头不是独立目标，也不是手写空头。它由 `//third_party/blink/public/mojom:mojom_platform`
这一 `mojom()` 目标产出，属于该 owner 的 248 个 IDL 之一：

- owner：`third_party/blink/public/mojom/mojom_platform.build_metadata`
- 自身 IDL：248 个，其中包含 `third_party/blink/public/mojom/browser_interface_broker.mojom`
- owner 闭包：119 个 module owner、777 个解析 module
- 两条原始 action：`shared`（1736 个输出）与 `normal`（1240 个输出），合计 2976

两种被排除的解释：

1. "缺少原始生成物"——不是凭空缺文件，而是整个 owner 从未执行过；单独补一个头会留下
   同一 owner 的其余 2975 个产物缺失，仍无法编译。
2. "选错 owner/variant"——`mojom_platform_blink__generator` 是 Blink 变体，
   产出的头名是 `*.mojom-blink-forward.h`，与所需文件名不符；`mojom_platform` 才是
   非变体 owner。`normal` 阶段还必须带原始 `--typemap`，否则 725 条原生映射语义丢失。

第三条"完整平台合同尚缺"确实存在，但不属于本模块：**旧 MG 诊断**（不是本轮 M）在
`ui/gfx/native_ui_types.h:189/239` 观察到 GTOS 平台分支缺失，属于窗口/平台合同，
需单独一轮，且不得用 `USE_AURA`/`USE_OZONE`、假冒 Linux 或无关 `NativeWindow`
typedef 绕过。

## 生成合同

合同只复用已经终验的产物，不重新解析、不重标身份：

- 777 个 `.mojom-module` 复用 `native-content-interfaces-20261018-a` run-02 的既有
  parser 输出，逐条核对 prior worker 的 argv、输出顺序与 import 闭合；
- 1 个 `mojom_platform__type_mappings` 与 1 个 `cpp_templates.zip` 同样复用原字节；
- 2 个 `.typemap_config` / `.ts_typemap_config` 沿用原始 GN 字节；
- 170 个 Mojo 运行时文件沿用原始快照，只把 `_ENABLE_MULTIPROCESSING` 保留为原始
  串行开关；
- 777 个 IDL 源文件按类分两处暂存：原始源在 `source/`，两个 `@gen/` 生成源在
  `build/gen/`，与真实 GN 目录一致。

两条 action 的 argv、rule 块、build 边、输出顺序、module 引用（1025 条，其中 248 个
自身 module 各重复一次）与原始 `toolchain.ninja` 逐字节一致，并保留原始 Blink
导出参数：`normal` 用 `--export_attribute BLINK_COMMON_EXPORT --export_header
third_party/blink/public/common/common_export.h`，`shared` 用
`COMPONENT_EXPORT(MOJOM_SHARED_BLINK_COMMON_EXPORT)` 与 `base/component_export.h`。

## 结果

两条 action 实际执行成功：`shared` 1736 个输出（532.95 秒）、`normal` 1240 个输出
（415.67 秒），共 2976 个新产物，全部输入、暂存、输出与别名在结束时重新哈希一致。
目标头 `browser_interface_broker.mojom-forward.h` 为真实生成物，声明
`namespace blink::mojom { class BrowserInterfaceBroker; }`，不是手写空头。

负例分三类，覆盖不同门槛：

- 作业清单绑定：缺失 module 输入、清单摘要被改、合同身份不符、未审 action，四例都在
  绑定门槛拒绝，未解锁生成、0 输出；
- 实际内容变化：保留与合同一致的清单摘要，只改复制夹具里第一个 module 的字节，
  由生产使用的读前校验拒绝（`Changed actual worker input`），0 输出；该例在真实
  Landlock ABI 7 入口内运行，guard 由实际应用隔离的 launcher 写出；
- 跨实例协议：16 个边界例覆盖设备漂移接受、内容/inode/readlink 变化、挂载来源、
  fstype、aname、非白名单路径、未知嵌套挂载、证据不全、基线建立后再变化，以及
  "实时采集变化/第二次采集缺失/采集失败/命名空间不符必须拒绝"，全部按预期判定。

## 跨实例复用协议

历史包记录的 `(st_dev, st_ino, st_mode)` 无法跨 WSL 实例复验：9p 挂载在实例重启后
重新分配 `st_dev`。本轮不是忽略该值，而是显式重新取得资格：

1. 只对固定列举的五个上游包与本轮新包生效，没有"任意路径忽略 `st_dev`"的开关，
   也不影响 Python、Clang、ELF loader、`/dev/null`、命名空间、执行权限或 Landlock。
2. 重新资格验证要求：inode、mode、节点类型、符号链接原始 readlink 文本、规范解析目标、
   文件内容摘要全部一致，且节点由其覆盖挂载解释。
3. 挂载证据按真实 `mountinfo` 解析（含八进制转义），核对 mountpoint、fstype、source、
   root、`aname`，并拒绝工作区挂载下的未知嵌套挂载。`/mnt` 祖先单独验证，不因位于
   `/mnt/f` 之下就放行所有设备变化。
4. 旧 `st_dev` 漂移只在上面的预检全部通过后，作为"当前重新取得资格的内容复用"记录，
   并写入逐节点明细与哈希绑定的基线。本次运行预检、执行与后验期间严格比较新基线，
   不再二次归一化。
5. 历史没有挂载证据，因此明确标记"历史挂载身份不可证明"，不声称同源已证实。

revision 2 实测：6 个包、10500 条观察记录、8121 个文件内容核对、6438 个节点接受设备
漂移（其中 5 个是 `/mnt` 祖先节点），0 个 inode/mode/类型/链接/内容变化。revision 3
把挂载证据改为实时采集后，按用户要求未重跑完整 consumer，因此这组六包数字属于
revision 2，revision 3 未重新确认。

注意计数口径：10500 是各清单的观察行数，跨包有重复；每包内部已去重，但包与包之间
仍会重复同一路径，因此不能称为 10500 个独立文件。

## 复现入口

生成包在 `artifacts/native-content-browser-interface-broker-20261018-a`（**不入库**）。
仓库内容本身不足以复现本次验证，需要该包内的以下脚本：

| 用途 | 包内脚本 |
|---|---|
| 合同准备 | `prepare-contract-v2.ps1` |
| 生成 controller / worker / launcher | `generate-blink-platform-v6.py`、`mojo-worker-v4.py`、`launch-blink-platform-v6.py` |
| 跨实例复用协议 | `requalify-v4.py` |
| consumer / launcher | `content-delegate-r8.py`、`launch-content-delegate-r8.py` |
| 轻量隔离入口（只跑 H1 小检查） | `launch-h1-v2.py` |
| 集成门槛检查 | `check-h1-v3.py` |
| 协议边界负例（r3 版本，未重跑） | `negative-protocol-v2.py` |
| 实际内容变化负例（r3 版本，未重跑） | `negative-content-v2.py` |

执行结构为 `wsl.exe -d Ubuntu-20.04` → `/usr/bin/env -i` 最小环境 →
`unshare --user --map-root-user --net` → `/usr/bin/python3 -I -B -S` 绝对脚本路径；
Landlock ABI 7、工作区只读、仅新 OUT 可写、无外网、子进程 `shell=False` 且只回收
自己的进程组。

## 同阶段去重与计数

`requalify-v4.py` 的 `StageContext` 在一个阶段内按规范物理路径只做一次完整摘要读取：
先收集所有观察的物理路径与预期摘要，同一路径出现不同预期摘要直接拒绝（后写不覆盖），
然后每个唯一物理文件读一次。节点检查（类型、inode/mode、readlink、解析目标、覆盖挂载）
仍逐条执行——**去重的是读取，不是来源资格**。

后验使用**新建**的 `StageContext`，因此不会复用预检摘要；不同阶段不共享缓存，也不用
mtime/size 替代摘要。

`Budget` 分开记录 `file_hash_operations`、`read_chunks`、`bytes_read`。空文件计一次
文件摘要操作、零个读块。v2 把每 1 MiB 块记作一次"hash_calls"，那实际是块数。

小夹具（数据 < 2 MiB）实测：4 条文件观察（其中一条经符号链接目录别名指向同一物理文件）
→ 唯一物理文件 3 个、本阶段唯一读取 3 次、文件摘要操作 3 次、读块 3 次、字节 1048588；
同一物理文件给出冲突摘要时拒绝；后验新建上下文重读 3 次并能发现字节变化。

## 符号链接的解析目标属于基线

基线同时保存符号链接的已验证 `target`（`realpath` 结果），后验在比对 `readlink` 文本
之后**重新**计算 `realpath` 并与基线 target 比较，且要求目标仍在固定工作区内。基线
缺少 `target` 字段时直接拒绝，不用当前值补成历史值。

只比较 `readlink` 文本是不够的：若顶层链接文本与 inode/mode 不变、而它指向的中间链接
改指向另一目录，最终解析目标会变，只有 target 门能发现。该情形已作为回归夹具实测：
基线记录 `top -> mid -> dirA`（`readlink=mid`、`target=.../dirA`），把 `mid` 改指
`dirB` 后，`top` 的文本与 inode/mode 均未变，后验以
`Symlink target changed after baseline` 拒绝。

## 挂载证据必须实时采集

`/proc/self/mountinfo` 是进程私有的：直接给它加 Landlock 规则只会绑定 launcher 自己
的 inode，子进程读同一路径会拿到另一个 inode 而被拒。因此 launcher 在应用 Landlock
**之前**打开该文件的只读描述符，经 `pass_fds` 交给子进程；子进程只重读这个描述符，
不自己打开 `/proc`。launcher 同时验证"沙箱建立后新开 `/proc` 会被拒绝"，把该拒绝
作为受控通路的证据。

保存下来的 `mountinfo.txt` **只是历史快照**，其完整性单独校验，绝不当作"当前挂载"。
基线、执行前与最终后验都重新读取描述符。比较范围是显式列举的字段：工作区挂载的
mountpoint、fstype、source、root、aname 种类与 path、device、其下不得有嵌套挂载，
以及祖先挂载的 mountpoint、fstype、device；记录 id、父 id、挂载选项等字段明确声明
不在比较范围内，而不是被悄悄比较。

采集器每次被调用都执行 `lseek(fd, 0)` 再读到 EOF，**不缓存**首次文本。轻量检查实测
基线、干净后验与变异后验共触发 3 次真实读取。实时表未变化时两次摘要相同是正常结果；
本项验证的是"描述符确实被重读"，不是"动态挂载变化已在真实系统注入"。

同一 mount 命名空间 inode 只证明两个进程看的是同一命名空间对象，不证明表内容未变，
所以它只用于"这两次采集属于同一视图"，不能替代内容比较。

生产 consumer 侧的同一条接线**仍未执行**，标记为 STATIC_ONLY。

## 当前障碍

revision 2 的完整 consumer 运行已越过 `render_frame_host.h:41`，新的首个报错是
`render_frame_host.h:45`：

```
third_party/blink/public/mojom/frame/frame.mojom-forward.h: file not found
```

revision 3 按用户要求未重跑完整 consumer，因此该 `:45` 结果仍是 revision 2 的实测
证据，revision 3 未重新确认。

`frame/frame.mojom` 属于另一个 owner `//third_party/blink/public/mojom:mojom_core`
（`BUILD.gn:1156` 起的 `mojom("mojom_core")`），不在本轮 248 个 IDL 内，因此本轮
2976 个产物里本来就没有它。这是下一轮要处理的独立 owner，不是本模块的遗漏。

`frame/frame.mojom` 引用的其它 `frame/*.mojom` 同样属于 `mojom_core`。

GTOS Content、浏览器、网页、视频与 HTML5 仍未运行；CXX、MD、ELF32 未执行。
