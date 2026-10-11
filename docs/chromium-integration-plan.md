# 32 位 Chromium 原生集成主线

2026-10-11 对照 [issue #9](https://github.com/awe0w0/GTOS/issues/9) 与本地构建证据校准。
本次重新检查最新创建和更新的 issue；#9 仍为最新，更新时间为
2026-10-09T15:37:06Z，暂无评论。issue 中的 1153b56 是评估基线。
最终目标是在 GTOS 客体执行真实 Chromium，显示并操作网页，再逐步验收网络、视频和
HTML5。当前交付架构为 IA32；x64 的内核态启动、帧池与稀疏 VM 是保留的备选证据。

## 当前证据与限制

本次日志消费者适配前 dev 与 origin/dev 为 5aeaba8c366e353069ec715416aaed11084ea43e；
完整构建入口在 82c93df 发布。9ad59b3 修正根 .gitattributes 改动触发的文件系统
源码锁回归，将 V8 属性限定在模块目录；未修改资格锁或放宽检查。
5aeaba8 的 [CI](https://github.com/awe0w0/GTOS/actions/runs/38131267697) 与 9ad59b3 的
[CI](https://github.com/awe0w0/GTOS/actions/runs/38130414120) 四项既有门禁均通过。此前 d9ace67 的
[CI](https://github.com/awe0w0/GTOS/actions/runs/38122735830) 四项既有门禁通过。
这些门禁均不包含完整 V8 或浏览器运行。
Linux 参考引擎运行已完成，GTOS 客体 Isolate、
JavaScript、Content 首帧、DOM 交互、网页导航、视频与 HTML5 尚未验收。

| 输入 | 锁定版本 |
| --- | --- |
| Chromium | 04bca2584cddb39e134fc7c16bb2886e02863d0e |
| Chromium DEPS 的 V8 / 独立 V8 | be042d4462bee463c9b785701b8c6ee4576ee0a3，两处一致 |
| libc++ | 97b436da4c33663581d394f4ee0a5977fc38c2f4 |
| LLVM libc | ebe33e01982dbbf879661e3b6b78450f3020a53f |
| Clang | llvmorg-24-init-7747-g62397f8b-57 |
| Rust 上游目标版本 | 1edd55dcfcd573872c727fa3e086369a71661ee0，子版本 1120；本次 V8 目标未消费 Rust |
| 构建主机 / 客体目标 | WSL Ubuntu-20.04 Linux x64 / GTOS i686-unknown-none-elf |

仓库现在提供 [完整目标模块](../apps/native_v8_integration/README.md) 和
`tools/build-native-v8-integration.py`。75 个平台覆盖、424 个私有/C++ SDK 文件和
438 个 LLVM C SDK 文件均按内容锁定；原生头搜索顺序为私有覆盖、上游 libc++、
C SDK、Clang 资源头。外部源码/工具及宿主运行库按版本和内容核对，构建输出必须
是新目录。该入口需要准备好的上游 DEPS/sysroot，不负责在空白主机安装依赖。

真正的 native_embedder executable 仍依赖完整上游 V8 与 libplatform，包含原生入口、
构造/析构数组和真实 Platform/Isolate/Context/Script 调用。完整 GN 图及实际 ELF
链接命令已经生成。修正 SDK 顺序后的冷构建 portable-cold-03 返回 1，首个诊断为
`third_party/abseil-cpp/absl/log/internal/log_message.cc:59`：`'unistd.h' file not found`。
缓存续编 portable-resume-02 的首个诊断是 log_message.cc:59 缺少 unistd.h，实际
消费者是 FailQuietly 的 `_exit(1)`。portable-resume-04 使用保留的 cold-03
缓存副本，加入当时尚未提交的日志立即退出适配；完整目标仍返回 1，首个诊断前进到
log_message.cc:218 的 `pid_t`（unsigned int）到日志 `tid_t`（int）的符号性冲突，
随后报告 seed_material.cc:17 缺少 fcntl.h。已有 GTOS sysinfo.h 覆盖使用无符号
ID，LLVM C pid_t 与 Abseil 日志类型使用有符号值；这一失败要求核对实际 ID 范围和
转换契约，不能直接关闭警告或用未经边界证明的强转掩盖。该轮 GN 依赖图未变、
绑定输入未变；输入快照、准确编译命令和完整失败日志已保留。该轮尚无客体语义
验收，保留为历史失败。随后为 GTOS 选择 Abseil 已支持的 uint32
日志 ID 类型，保留原生 ABI 的完整 ID 范围及 Linux 原分支；不通过强转或关闭警告
处理符号性冲突。最新完整续编 portable-resume-05 越过日志消费者，首个失败变为
`seed_material.cc:17` 缺少 fcntl.h。该轮输入和依赖图仍未变，保留为历史失败。
随后实际熵消费者接到 GTOS CPL3 CPUID/RDRAND 源；空指针、块数/地址溢出和
当前用户范围显式检查，不支持或十次取数失败时返回 false，没有固定种子回退。
portable-resume-06 越过该对象，当前首个失败为 futex.h:31 缺少 unistd.h，随后
还报告 mutex.cc:65 缺少 fcntl.h。实际宏预处理确认独立 GTOS 身份，未选择 Linux
futex 或 POSIX semaphore；但 pthread waiter 以非 Windows 条件假定能力，优先于
标准 C++ 回退。须修正 GTOS 能力选择，再核对候选标准 C++ waiter 的实际等待。
不能把未用分支的头依赖机械当成必须实现 Linux futex。完整 ELF 仍未生成，单任务锁
仍不是通用线程。
此前同图 attempt-07 还实际暴露了 seed_material
熵读取、futex 等待、时区文件读取及 tm_gmtoff/tm_zone 接口缺口。并行编译的首个
报错次序随缓存变化，保留每轮原始诊断，不将头文件声明当成服务实现。

上述完整 GN 依赖清单 SHA256 相同，未删失败对象。完整 ELF、链接与客体运行
仍未通过。初版 portable-cold-01 错误地合并 SDK，将 C 头放在 libc++ 包装头前，
属于本集成入口的配置错误；失败版本、输入和日志已保留，不算 OS 根因。
当前已从规定的准备输入和全新输出重放真实阻塞，尚无空白主机 bootstrap 验收。

动态 ELF/VDSO 能力适配保留 __ELF__ 与独立 __GTOS__ 身份，排除目前静态装载
模型不提供的动态设施；实际 Linux IA32 对照仍保留原能力。日志立即退出的部分
消费者接到 GTOS Exit ABI，信号安装调用明确报错并触发用户故障；完整目标已
前进到熵读取消费者。[退出验证](../apps/native_v8_exit_probe/README.md) 使用实际
Abseil 函数和完整目标原生入口，复用哈希一致的 FILE/内核对象。O0/32 MiB/单 vCPU
与 O2/64 MiB/四 vCPU 均在 CPL3 完成六个任务及精确回收；正常返回执行自动、TLS
和 fini 析构并刷新缓冲，立即退出/故障跳过；活跃用户同伴、ring0 和启动任务继续
推进。两组各有 138 个观察器负例，内核仍通过原整数指令审计。信号处理仍不支持，
完整日志格式化和完整 V8 不由这些消费者夹具验收。机器清单见 chromium-integration-status.json。

[实际熵消费者验证](../apps/native_v8_seed_probe/README.md)使用冻结完整目标中的
真实 ReadSeedMaterialFromOSEntropy 和原生入口，O2 对象与完整目标逐字一致。
O0/32 MiB/单 vCPU 与 O2/64 MiB/四 vCPU 分别开启和关闭 RDRAND；四组各六个任务
精确回收、同伴继续，各有 164 个观察器负例。开启源时验证跨页、字节尾部、长度
溢出和实际未映射/只读页故障，关闭源时返回 false 且缓冲保持原值。没有新增随机
syscall、扩容或 ISA 豁免；不验收 GetSaltMaterial、熵池、真机熵质量、TLS 或完整 V8。

真实 V8 heap-profiler.cc 的编译失败指向 output-stream.h:27 的 basic_ofstream 未定义。
原 SDK 的 _LIBCPP_HAS_FILESYSTEM=0 隐去了文件流定义。独立覆盖配置后，
heap-profiler.cc 和 sort-builtins.cc 均编译通过，但仍引用真实 libc++ 流与 locale 符号。
启用上游 C++26 库规则后，还暴露 fstream 的原生句柄分支缺口。独立 GTOS 分支接到
已有 fileno 后，ios.instantiations.cpp、ios.cpp 和 locale.cpp 编译通过；符号清单仍包含
setbuf、locale、宽字符转换、错误分类及静态析构等依赖。这些是编译实验，尚未接入完整
GN 链接或验收客体文件流。仅完成底层 FILE 不会自动解决全部 C++ 运行库依赖。

固定 Chromium 的普通根 BUILD.gn 明确拒绝 Linux IA32（1533–1549 行）。使用
--root-target=//content/shell:content_shell 可以生成 IA32 目标图与命令，目标是
linux:clang_x86 下的 testonly executable。指定根绕过了普通根的架构断言，因此只能
证明该图可生成，不能声明官方维护支持、实际编译/链接通过或 GTOS 平台可用。

本地日志保存在 GTOS-Chromium 工作区的 artifacts/native-v8-integration-20261011-a：
attempt-04 保留完整目标超时，attempt-05 保留最初的 ELF 能力错误，
attempt-06 保留故障处理/日志失败，attempt-07 保留后续熵、同步和时区失败；abseil-static-elf-01 与
abseil-static-elf-identity-02 保存 ELF 能力适配和 Linux 对照，
abseil-native-errors-01/02 保存四对象编译及风格复审；
direct-dependencies-01 是同图原生依赖诊断；
fstream-config-diagnostic-01 和 stream-runtime-diagnostic-03 是独立配置/流实验；
content-shell-linux-ia32-01 与 content-shell-target-evidence-02 分别保留根断言和目标图证据。
portable-cold-01 保留配置错误，portable-resume-02 保留修正后的续编，
portable-cold-03 保留新输出的冷构建失败，portable-resume-04 保留未提交日志适配后的
完整续编失败，portable-resume-05 保留日志适配后新的熵读取阻塞。
exit-guest-01 保留夹具误用 Reap API 的编译失败；exit-guest-02 保存修正后的客体验收，
public-exit-guest-01 保存仓库内参数化工具的初次重放。复审发现普通 Python 生成的
缓存目录被当作文件读取，导致缓存负例提前失败；原复审报告及失效说明保留。
public-exit-guest-02 使用普通 Python 保留缓存重放，并以准确对象哈希拒绝验证负例。
公开模块提供重放入口；完整引擎运行仍待验收。

## 阶段与验收

| 阶段 | 工作 | 完成证据 |
| --- | --- | --- |
| P0 | 固定架构、上游源码/工具链、完整 embedder 与 Content 图；发布补丁和失败清单 | 准备好的 pinned 输入和新输出已重放完整目标失败；空白主机 bootstrap 与完整链接尚未验收 |
| P1 | 补足真实 ELF 所需装载、运行库与内存能力 | GTOS 内真实 Isolate 执行 JS，并精确回收；随后逐项加入数组、TypedArray、GC、microtasks、浮点与异常 |
| P2 | 由真实 V8/Content 错误驱动线程、文件、IPC、共享内存等服务 | 实际消费者前进到下一阶段；边界、低内存、取消、同伴存活与回收通过 |
| P3 | Content Shell、本地资源、软件 Skia/Ozone、窗口及输入 | 客体 HTML/CSS 首帧、鼠标/键盘触发 JS 更新 DOM 与重绘，关闭及 renderer 故障可回收 |
| P4 | 受控 HTTP，再 DNS/TLS/证书、HTTPS 与进程/沙箱安全 | 客体自行获取和显示受控页面；公网浏览须独立安全验收，音视频/GPU 后续专项推进 |

每轮先记录真实完整目标的失败，再实现最小正确服务并重放同一目标。独立探针用于证明
所需服务的语义与隔离，避免用接口数量作为浏览器进度。当前 bring-up 显式使用
单线程、JIT-less、无 WebAssembly/ICU/sandbox 的诊断配置；这些限制不能作为最终浏览器
功能或安全能力。缺失操作应明确失败，不伪装 Linux，不提供假成功占位实现。

保持进程槽 4、用户页/ELF 页 256、动态区域 32、堆 64 KiB、可用用户栈 8176 字节及
陷阱栈 16 KiB。先测真实 ELF、地址空间、栈、堆/RSS，再联动修改帧池、页表、配额、
元数据、所有权、回滚及回收。8364 字节 long-double 格式化帧问题仍保留。
约 1.35 TiB 的 x64 参考虚拟预留不等于 IA32 的物理内存需求。

IA32 后续若遇到目标版本无法合理支持，或地址空间与隔离要求无法满足，提交包含
完整失败证据和 x64 门禁差额的架构决策；用户指定的 32 位目标不会被静默替换。
现有 x64 门禁仍缺 ELF64 用户态装载、CPL3/ABI、TLS、线程和浏览器服务运行证明。

## 紧接着执行的工作包

1. 根据完整目标 futex.h:31 的失败和真实宏证据，排除未选用 Linux futex 分支的
   POSIX 头依赖，修正非 Windows 条件对 pthread 能力的误判；核对候选 StdcppWaiter、
   mutex 的实际调用及单任务/等待能力，
   最小正确适配后重放同一 native_embedder。保留全部依赖、警告和失败日志。
2. 由新实际报错驱动同步、时间/时区等契约。复用已有已验收组件，不扩展无关叶子
   清单；已有退出客体证据持续保留，信号处理仍明确失败，不能报告已安装。
3. 每次必要适配后重放完整目标，依据实际链接符号复用 C FILE/libc++；确定性种子
   不等于系统熵，诊断单任务锁不等于原生线程或 futex。冷构建和缓存续编分别记录。
4. 完整 ELF 生成后记录段、映像页、映射、栈与堆需求，再联动处理装载/容量；以
   客体 Platform → Isolate → Context → Script 校验与回收作为首项功能验收。
   Content Shell IA32 可行性保持旁路核对，不能只凭指定根 GN 成功判断。

PNG 桌面、文件 ABI 和 C FILE 成果继续复用；生产挂载与磁盘 PNG 接线仍待完成，
按真实引擎资源需求接入。暂缓无主线消费者的 API 扩展、桌面美化和一般硬件专项。

## 迭代报告与回归

每次报告：最远真实阶段；当前首个客观阻塞和准确命令/日志；修复使目标前进到哪里。
新增服务须注明实际消费符号、语义与 guest 证据。构建驱动失败、超时和主动中断应单独
记录，不能伪装为 OS 根因，也不能跳过已知失败对象后报告完整构建成功。

模块修改运行必要正反例与对应真实集成路径；子系统修改触发隔离、VM、线程/FP、
存储中断及回收专项；阶段性运行完整历史回归。保留旧 WIP、缓存、失败日志与可见 VM
磁盘。生产镜像更新时刷新并重启可见 VM。自审、必要测试后正常提交和推送 dev，
不强推；只报告确实运行的 CI。
