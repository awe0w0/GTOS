# GTOS 原生 IA32 PartitionAlloc 平台基础

本模块把 Chromium PartitionAlloc 的进程/线程身份、errno 作用域、诊断输出和睡眠
接到已有的 GTOS 原生服务。实际 `allocator_base` 的 17 个成员已组成静态归档，
新消费者与真实 V8 完整对象缓存链接，并在 GTOS CPL3 中完成普通和 FATAL 两条路径。
这项验收覆盖所列平台接口；完整 PartitionAlloc 分配器和 Chromium Content 仍待接入。
Chromium 派生文件的版权头保留，许可证见 `CHROMIUM-LICENSE.txt`。

## 源码与 ABI

`overlay/` 的 20 个文件包含 16 个 PA/原生服务文件，以及三个 SDK 构建配置和一个
外部线程声明头。GTOS 保持独立平台身份，混入 Linux/Unix/Windows 等身份会拒绝编译。
ProcessId、ThreadId、线程引用和句柄均使用原生 32 位标量，0 表示空身份。身份来自
真实自查询系统调用，不访问分配器、缓存 TLS 或宿主线程库。

显式设置 `gtos_native_sdk` 与 `gtos_clang_resource_dir` 时，IA32 工具链选择已有
LLVM libc、libc++ ABI1/`__gtos_isolate_diagnostic`、外部线程声明、SSE2 和 emulated TLS。
SDK 参数为空时保留原标量配置；其他平台的配置分支保留。PA 的 61 个生成标志未改。
完整 V8 入口继续使用原有编译参数；新增消费者使用真实 PA GN 参数，二者只通过
`extern "C" int GtosPartitionPlatformChecks()` 连接。

`sdk-reuse.json` 逐文件绑定 866 个已有 SDK 基础文件。准备独立源视图时，先从
`apps/native_v8_integration` 复制这些文件，再按原相对路径应用 `overlay/`；覆盖文件
优先，包括两个原生 ABI 头。不要复制 C 专用的 `c-sdk/__config_site`，以免覆盖实际
外部线程配置。SDK 内容和原生运行库必须来自同一个已验收 ABI。

诊断直接使用 GTOS `SYS_WRITE`，每次最多 256 字节；只按实际返回的正数字节数前进，
遇到 0、错误或超长返回值即停止。它不要求 POSIX 文件描述符、stdio、堆或 TLS。
`Sleep` 使用已有单调时钟 `nanosleep`；非正时长直接返回，服务失败触发真实故障。

## 已验收

普通路径在运行真实 V8 脚本前检查六组条件，全部通过才得到掩码 63：

- 重复核对原生进程 ID、线程 ID、引用和句柄，以及空身份。
- 嵌套 errno 清零/恢复；内层和外层的原值分别保留。
- 被抑制、空指针及空字符串输出；256 字节完整行与跨块的 257 字节字符串。
  两个可见输出合计恰好 514 字节，包括换行，与内核实际写入计数一致。
- EINVAL 数字错误文本及原 errno；处理器实际收到一次完整消息。
- `SafeSPrintf` 截断返回长度和缓冲区终止，以及 INT64_MIN 的完整输出。
- 负、零和 2ms 睡眠；真实单调时钟确认正睡眠已过去至少 2ms。

同一个 V8 Isolate 随后完成原有对象/数组、TypedArray/DataView、真实 GC、Promise
微任务、预期异常及异常后的恢复。掩码为 127、GC 回调 2 次，完整原生终结后为
stage4/result0/exit0，无用户故障。

FATAL 路径先分配并逐字节校验 128 KiB，保持映射存活，再调用真实 `LogMessage`
析构路径。输出恰好 51 字节。GTOS 当前断点门为 DPL0，IA32 `int3` 因门权限触发
#GP(13)，错误码 `0x1A`；内核记录实际异常并终止整个进程。参见
[Intel 中断门权限规则](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf)。
异常后的标志和返回值均未执行。内核观察器确认 32 页动态映射及端点内容仍在，
随后 `Reap` 精确归还镜像、栈、堆、页表及元数据帧。

| 路径 | 配置 | 关键结果 | 回收后空闲帧/基线 |
| --- | --- | --- | --- |
| 普通 | 32MiB / smp4 | PA63、V8 127、GC2、6254 次系统调用 | 4423 / 4423 |
| 普通 | 64MiB / smp1 | PA63、V8 127、GC2、6254 次系统调用 | 12607 / 12607 |
| FATAL | 32MiB / smp4 | vector13/error0x1A、32 页待回收 | 4424 / 4424 |
| FATAL | 64MiB / smp1 | vector13/error0x1A、32 页待回收 | 12608 / 12608 |

四次运行均检查 CPL3、私有 CR3、仍在推进的内核任务、栈两侧保护页和精确回收。
smp4 表示四个虚拟 CPU 的机器配置，用户任务仍按现有 BSP 调度契约执行。

## 证据与重放

`acceptance.json` 是本模块的范围和观测值；`receipts/` 保存实际编译、完整链接、
来宾和自审原始记录，`logs/` 保存四份原始来宾日志。`source-lock.json` 绑定复制
文件，`provenance/` 保存固定 Chromium/V8/libc++ 的源码审计、真实 GN 行、完整 MD
头文件闭包和归档符号清单。失败运行保留在原产物目录，摘要见
`historical-rejections.json`。

`replay/` 是已执行入口的逐字节源码快照，固定工作区原入口位于：

- `F:/GTOS-Chromium/artifacts/native-partition-platform-20261016-a/`
- `F:/GTOS-Chromium/artifacts/native-partition-consumer-20261016-a/`

它们依赖现有已锁定源码、GN 准备结果、V8/原生运行库对象和 25 个内核核心对象。
入口拒绝已有输出目录，也不接受任意路径或工具覆盖。仓库快照保留原路径校验，
不能直接改路径运行并宣称空白主机 bootstrap 或完整冷构建通过。

执行遵守[本地执行栅栏](../../docs/native-command-execution.md)：固定工具摘要、
Python `-I -B -S`、最小环境、独立无网络命名空间、no_new_privs、Landlock 精确执行
文件和新目录写入限制。未执行 Ninja、宿主测试 ELF、下载命令或凭据读取；构建
入口不触发 Git 或通知。来源和缓存保持只读，全部实际输入及别名终验通过。

## 待完成的边界

归档包含的时钟回调、RandBytes、数学及浮点格式化外部符号尚未全部运行验收；
当前真实链接的节回收可能移除未消费的接口。短写/零写/错误返回的循环分支已做
逻辑审查，本次未向真实诊断服务注入这些返回值。17 成员归档不能代表 PA 核心分配器。

完整 Content GN 图的目录和生成元数据来源审查、Dawn common、Rust std、libxml
线程/fd/ICU 后端仍有待办。继续从真实 Content 消费者的首个失败推进本地页面首帧、
输入和 DOM，再进入受控网络、视频与 HTML5。原生浏览器尚未运行。生产镜像未更新。
