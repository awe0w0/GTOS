# 32 位 Chromium 原生集成主线

当前交付架构保持用户选定的 IA32，目标仍是 GTOS 客体真实 Chromium 网页浏览、
视频与 HTML5。按 [issue #9](https://github.com/awe0w0/GTOS/issues/9) 先推进真实
V8 Isolate/JS，再到 Content 本地首帧与 DOM、受控网络及音视频。

## 当前证据与下一步

2026-10-15，真实GN的`//build/rust/std/rules:core`已按O3/debug assertions编译通过：
393个实际依赖闭合，16个ELF32/i386代码对象，全部输入及别名终验通过。新增的IA32
Rust能力边界同时保留x64纯C++资格图。详见[GN core模块](../apps/native_rust_target/gn/README.md)。
这只解决一个Content前置，完整std、C互调用与Rust客机程序仍未通过。

2026-10-15，[IA32共享链接规则](../apps/native_shared_link/README.md)完成真实GN规则核对、
三个ELF32共享产物、PIC/GOT/PLT及SONAME/NEEDED检查，非PIC和未定义符号被真实LLD拒绝。
三文件补丁在新目录实际应用并逐字节核对；x64纯C++图仍可生成。此处仅验收构建能力，
未构建真实base测试辅助库或验收GTOS动态加载器。

2026-10-15，[NASM格式适配](../apps/native_nasm/README.md)补充GTOS IA32的ELF32分支。
真实NASM生成调用者/提供者及两份未改libaom SSSE3对象，依赖、符号、重定位及静态
链接通过，缺失include和ELF64负例被拒绝。没有执行滤波函数或验收完整libaom平台。
Content Shell精确root-pattern图继续使用通用菜单模型准入和独立libxml配置候选。
原始libxml解析器/字符表已编译为IA32，但pthread.h/fcntl.h等接口仍缺，候选运行时未验收。
2026-10-15，[Rust可执行链接规则](../apps/native_rust_target/bin/README.md)完成真实GN
两目标/34文件闭合，Rust/C/64位除法静态ELF及缺失提供者负例通过；已有core和本轮
真实compiler_builtins提供元数据与内建函数。完整图已越过rust_bin错误，当前首错为
Linux次级工具链`printing/BUILD.gn:482`的CUPS探测限制。GN仍返回1，没有Content产物。
时间戳和品牌信息仅允许锁定脚本及固定参数；显式`use_kerberos=false`和
`use_official_google_api_keys=false`用于开源本地首帧准备，未探测内部密钥。完整图仍返回1。
下一步审查宿主CUPS探测，再继续完整Rust std、libxml线程/fd/ICU及真实libaom配置；
不能直接把Linux平台宏当作GTOS能力。旧失败保留，不按测试名字删除目标。

2026-10-15，完整62在同一真实原生V8入口上补验对象/数组、ArrayBuffer/TypedArray/
DataView、MarkSweepCompact GC回调、显式Promise微任务和预期异常后的恢复。两配置
均要求掩码127、GC回调大于0、stage4/result0/exit0及精确Reap。仅替换main对象，
另外1109个链接输入及25个客机内核核心对象保持。详见[补验模块](../apps/native_v8_launch/workloads/README.md)。

2026-10-15，完整61的真实IA32 V8 ELF已在GTOS用户态完成Isolate、Context、
Script与退出：两种内存/CPU配置均检查得到GTOS42和3.75，stage4/result0/exit0，
无故障，所有进程物理帧精确回收。详见[原生脚本运行模块](../apps/native_v8_launch/README.md)。
完整1110输入保留真实GN顺序，仅替换堆/入口；冷构建与空白主机bootstrap仍未验收。

实际ELF为12,932,528字节、2385镜像页。按需后备arena解决真实堆容量及槽耗尽；
显式512KiB根栈和1024运行页预算解决8KiB guard故障和194+64页的配额失败。
初始2513驻留页、14元数据帧，总容量3537条。普通入口及256页VM事务限制保留。
页记录全量检查用每次重建的精确索引替代二次扫描；别名/权限/所有权检查仍执行，
碰撞、损坏映射、物理OOM、两端guard、默认槽复用、共享堆与精确回收均通过。

下一步按issue #9推进真实Chromium Content本地首帧和DOM，再接受控网络、
网页导航、音视频及HTML5。浏览器、视频、HTML5、Wasm和通用用户SMP尚未验收。
实际Content Shell目标引入Rust；GTOS工具链仍是freestanding资格工具链。
私有目标图已实际执行，Rust身份候选及必要配置使首错前进，但完整图仍返回1；
没有通过Content对象构建。继续处理宿主工具配置、libxml/libaom、共享库运行时边界，并按真实
Rust std消费者接入OS后端。未知平台no_threads与target_has_threads的冲突仍未解决。

栈迹/共享边界的源码和必要回归见[对应模块](../apps/native_debug_shared/README.md)：
真实直接调用者PC、原始地址输出及关闭stderr；共享映射和命名显式失败，误用共享
释放不删除私有页。不将这些失败边界记作共享内存、IPC或HTML5完成。生产镜像未改。

## 历史第44次链接：线程生命周期

2026-10-14，完整44的实际首错为 `v8::base::Thread::Thread(Options const&)`。
有界原生线程生命周期已通过32M/smp1及64M/smp4客体：每个上下文有独立CPL3帧、
内核/用户栈与FP记录，worker共享root地址空间及进程资源。44个上下文精确回收，
真实创建/join、栈保留、整组取消、worker页故障和跨进程join拒绝均通过。
候选内核默认关闭线程时，原真实V8 FOpen另通过8任务/8精确回收。
详见[线程生命周期模块](../apps/native_thread_lifecycle/README.md)。

共享堆已加入实际原子锁和BSP Yield，覆盖首次arena发布、链表拆合、realloc及释放。
同一抢占点的旧heap报0x48000002，新heap在双配置24上下文中完成701542项检查并
精确回收，默认FOpen另通过8任务回归。详见[共享堆模块](../apps/native_thread_heap/README.md)。
这解决TLS首次分配的前置竞态，不代表TLS/errno或V8线程已经具备共享资格。

此后已推进每线程TLS/errno、共享锁、libc++abi guard及真实Thread/Start/Join/
Semaphore，后续证据保留在下文与机器清单。诊断完整目标仍使用单用户线程配置；
第44次链接时的剩余工作只作为历史记录，最新阶段以上方完整61原生脚本运行为准。

## 历史第33次链接：底层分配器

2026-10-13，第33次真实完整目标链接已越过LowLevelAlloc。保留固定Abseil的arena、
skiplist、拆分合并与计数，接入GTOS VM页服务，并按原始reservation句柄释放。
实际GN重编译6个受影响对象，新增1个页后端对象，5149条旧编译命令和引擎参数
保持。32MiB/单CPU与64MiB/四CPU共12个任务完成正常、溢出、重复释放、配额失败
和取消检查，14次精确回收，508个日志反例被拒绝。

当时首错为`std::__gtos_isolate_diagnostic::mutex::lock()`，由StdcppWaiter消费。
后续已按真实消费者推进单用户任务同步模块；不能用空锁或无条件成功消除链接错误。
首次TLS分配仍使用C堆，真实共享线程的TLS与同步资格见上面的当前阶段。
完整ELF、Isolate/JS、网页、视频与HTML5未通过，生产镜像和容量未改。
证据见[底层分配器模块](../apps/native_low_level_alloc/README.md)和工作区
`artifacts/native-low-level-alloc-20261013-a/full-link-33`。

## 上一节点：原生字符串扫描

2026-10-13，第32次真实完整目标最终链接已使用新增的 `sscanf` GN 对象；
5148条旧对象命令、原输入顺序和引擎参数保持，旧缓存只读，全部绑定输入及旧缓存
元数据复核一致。`sscanf` 未定义错误已消失，实际首错为：

`ld.lld: error: undefined symbol: absl::base_internal::LowLevelAlloc::Free(void*)`

该扫描对象逐字节用于32M/单CPU与64M/四CPU两次GTOS冷启动，共14个用户任务、
164次正常扫描和16次精确回收；内存不足及恢复、输入/格式读取页故障、输出写入
页故障、扫描循环取消和恢复均通过。长双精度检查包含2^53+1、2^64−1与1+2^-63。
独立审查拒绝594个错误日志与5个不合格指令样例，实际53个带长度函数符号中的
13541条指令符合IA32/x87/SSE2范围。852字节仅是扫描器自身最大帧，未证明完整
调用链栈上界，也未证明取消时内部临时扫描缓冲仍存活。

当时LowLevelAlloc对象已在真实GN链接中，但平台缺失判定排除了实现；该阻塞现已
通过上述原生页后端推进。单任务TLS首次访问仍使用真实C heap，不能把后端无malloc
描述为整个allocator冷启动完全独立。

完整ELF、客体Isolate/JS、网页浏览、视频和HTML5仍未通过。生产镜像与容量未改。
第32次证据位于工作区 `artifacts/native-v8-integration-20261011-a/sscanf-full-link-32`，
客体与独立审查位于 `artifacts/native-scanf-guest-20261013-a`；新增源码已在本地
集成模块落盘并锁定。仓库参数化工具已完成双冷启动与独立审查，客户机ELF与私有
验证对应产物逐字节一致。仓库源码包独立GN准备通过，5149条对象命令、完整依赖
图与有序最终输入在路径归一化后保持；只显式固定原宿主链接池深度3。该准备步骤
未重新编译对象，参数化客户机验证仍复用原编译上下文中已验收的扫描器及支持对象。
后续执行继续遵循[执行栅栏](native-command-execution.md)。

## 历史观察（第 48–49 次交接）

时间格式化 dev@731a970、chrono dev@f4425da、宽字符 dev@b8f746b 已正常推送并核对远端。
本次由第30次真实首错驱动，保留固定 libc++ runtime_error 无异常终止函数体，复用
已验收的原生诊断输出。私有和公开离线双冷启动、自审通过：12任务、14精确回收、
200观察器反例；公开用户ELF与私有对象逐字节一致。

第31次直接重放当前GN真实最终链接，保留5147条旧对象命令、原输入顺序和引擎参数，
旧Ninja缓存只读，仅新增1个实际必要对象。未重跑完整Ninja编译，也未复制整套缓存。
链接用时3.04秒，runtime_error首错已越过，实测下一首错：

`ld.lld: error: undefined symbol: sscanf`

它由固定locale.cpp的money_get<char/wchar_t>::do_get长双精度转换消费。下一步按实测
消费者实现必要扫描语义，检查数字、长双精度、源/容量、错误及回收，再回放同一目标。
完整ELF、客体Isolate/JS、浏览器、视频和HTML5仍未通过，容量及生产镜像未改。

所有未来执行沿 [安全栅栏](native-command-execution.md)：固定指纹与参数、最小环境、
临时无外部网络空间、新输出、旧失败/缓存保留。禁用内联Python、多层shell和失败日志
命令直接执行。推送与QQ通知单独按既有授权执行，02:00–09:00上海时间静默。
原有Linux Content第09次运行作为参考保留，不把参考编译或叶子通过算作客体浏览器验收。

证据见第48次交接和 apps/native_runtime_error/acceptance.json；这是正常dev提交前的
资格快照，提交推送及远端SHA核对另记发布回执。

## 历史观察（第 40–42 次交接）

当前详细证据见第 40 次交接。时间格式化已通过公开工具的实际双冷启动和自审，
实际第 28 次提供者与已执行对象字节一致，strftime 首错已越过。第 28 次实际 Ninja
用时 4211.15 秒，宿主重编译后完成最终链接；首错顺序与预期不同，chrono::steady_clock::now
位于最前。预测 mbsrtowcs 为首错的辅助审查失败保留，已按实测顺序校正；没有重启
完整构建。1262 份输入已冻结，原命令、链接输入与参数保留。

本模块尚未提交推送；下一项按 Abseil KernelTimeout 的真实 steady_clock 消费者推进，
保留单调性、时间单位和错误终止。UTF-8 私有候选暂不接入。生产镜像本次未更新。
完整 ELF、客体 Isolate/JS、浏览器、视频及 HTML5 仍未通过。

## 历史观察（第 36–39 次交接）

当前详细证据见第 40 次交接。标准 FILE 与已资格运行时已正常推送
dev@5a8f10a，远端 SHA 已核对。时间格式化公开源码、中文说明和参数化工具已进入
工作树；当前仅私有双冷启动与自审通过，公开重放、完整 28 终态和本模块提交仍待完成。
第 28 次实际 Ninja 的驱动为 210759，Ninja 为 211866；继续观察同一运行，不因
超时或等待重启。可见生产镜像本次没有更新，虚拟机与原磁盘保留。

公开时间格式化观察器预审通过，154 个错误证据样例被拒绝；这不是公开冷启动。
等待真实第 28 次终态期间，只读追踪了已观察到的 libc++ 宽字符消费者。固定 LLVM
内部转换器的宿主复现确认接受 C0 AF、ED A0 80、F4 90 80 80，对应过长编码、
代理码点与超范围值。原源码未改、未新增提供者、未作客体资格声明；若下一真实
阻塞进入该家族，需保持原源码并对标量/最短编码及状态、源指针语义作最小正确适配。

第 38 次已将顺序验收辅助流程附加到同一原生 28：等待真实终态，然后核对并
冻结输入、公开双冷启动重放和自审。辅助 PID 为 216458，执行会话为 71540；
原生驱动 210759、Ninja 211866 和执行会话 95792 保持运行。旧等待辅助流程因
目录层级与并发 JSON 读取问题被替换，未执行验收命令，原构建没有重启。
辅助流程不自动提交或推送；实际公开证据完成后仍需复审差异和验收记录。

第 39 次在独立私有头文件副本准备最短编码和 Unicode 标量检查。12 组合法边界
往返、20 组分段续接和 13 组无效编码拒绝通过宿主检查，原转换器对同一检查返回
失败，原始上游文件和状态布局保留。此候选未接入原生目标，未作 IA32 编译或
GTOS 客体资格声明；当前原生 28、公开时间模块及验收流程均未改变。

以下是各次记录保留的历史观察；其中的“当前/尚未提交”等措辞只描述当时状态。

## 历史观察（第 31–35 次交接）

当前详细证据见第 35 次交接。生产镜像没有更新，可见虚拟机保持运行；未提交修改、
原缓存和旧失败均保留。以下记录按当时观察保留，不能用其中的“当前/最新”判断今日状态。

标准流首个实际客体执行已到达输出分块、指针错误、输入/seek 错误和缓冲刷新，
随后在夹具的“重定向句柄必须大于 2”断言失败。PNG 种子使用句柄 1，首次重定向
使用合法原生句柄 2；原生句柄为正值，不能套用 POSIX 保留编号规则。原失败保留，
独立 source-v2 校正该断言，并增加控制台 fileno 明确 ENOSYS 的三项检查。控制台
没有原生描述符服务，不返回虚构的 0/1/2；重定向后返回真实句柄。新提供者仍为
92 字节最大自身帧，无动态静态初始化，qemu-06 正执行，尚未验收整个模块。

标准流资格已在 32M/1CPU 与 64M/4CPU 两次冷启动完成，903 项输入未变；
每个任务 46 项标准流检查，真实输出、分块、指针错误、缓冲、重定向与关闭均通过。
正常关闭后堆最后分配已释放，旧句柄查询失败且内核确认堆不可读；故障、取消和
互斥拒绝时两个句柄与 17 帧堆保留到 Reap 后精确回收。旧 TLS 堆假设的实际失败
保留；补充更直接的区域/映射检查，提供者未改。完整 27 从冻结的 26 缓存副本
准备，尚无新完整终态，标准流首错是否越过须以实际链接为准。
Content 09 已进入真实 Ninja，原 bytecodes 生成失败动作已前进，输出与独立诊断
61394 字节逐字一致。此为宿主参考编译进展，仍不能计为 GTOS 浏览器验收。

完整 27 于 18:26:18（上海时间）最终链接返回 1，首错前进到 strftime；
输入未变，实际标准 FILE 与客体执行对象字节一致，1261 份私有输入冻结通过。
公开源码、中文说明和参数化标准流工具已进入工作树；公开两次冷启动及自审通过。
公开 GN 入口生成的全部 5135 对象命令、顺序链接输入和参数与实际完整 27 一致；
prepare-only 不计为完整编译，空白主机 bootstrap 仍未验收。公开用户 ELF 的源路径
和对象顺序与私有夹具不同，整 ELF 字节不同；各真实提供者、源码、装载几何和
新的客体执行均核对，不错误声称整 ELF 一致。目前尚未提交或推送。

## 历史观察（截至第 30 次交接，以以上当前状态为准）

实际 LLVM-libc/no_locale 后端所需的三项 C-locale 原提供者已独立编译通过。
仅这些源文件选择其类型/宏头要求的 LIBC_FULL_BUILD，类型头与现有 SDK 字节一致。
原 provider 算法未改，newlocale/freelocale 的 C 符号和 C-locale 数据已定义；数据
对象无函数，两个函数最大自身帧为 4/0 字节。初次类型模式失败和随后检查器误要求
纯数据对象存在 .su 的失败均保留。当前仅编译通过，名称/掩码与生命周期的客体
验证及完整 26 重放尚未执行；完整 25 的 newlocale 首错不能先宣称已越过。

完整 25 于 14:51:44（上海时间）最终链接返回 1，输入未变；首错已从
__cxa_guard_acquire 前进到 newlocale。5130 条原对象命令、原顺序链接输入和引擎
参数保留，两项实际 guard 对象与客体已执行提供者逐字一致；1261 份输入冻结通过。
实际 Ninja 7 个任务、98.1 秒，缓存准备另计。完整 ELF 和客体 Isolate/JS 仍未通过。
实际 locale.cpp 预处理选择 LLVM-libc/no_locale 后端，使用 newlocale/freelocale
构建并释放 C locale，没有使用其他平台的 uselocale 恢复包装。下一工作包按真实
消费者接入固定 C-only 提供者；初次编译暴露其内部类型/宏头要求 LIBC_FULL_BUILD，
所需类型头与现有 SDK 字节一致，配置修正草稿在准备，仍未完成 locale 客体验证。
Content 参考 08 已完成缓存准备和 GN，进入实际 Ninja；保留原源码和此前失败，
不能把其参考进度计为 GTOS 网页验收。下方较早状态保留为历史，当前终态为完整 25。

固定 libc++abi 的原 guard 算法已选择官方 NoThreads 实现，接到真实诊断/abort
终止桥接；原上游文件未改。三项 C ABI 提供者编译通过，没有 mutex/thread-wait/
futex 依赖。两次 QEMU 冷启动直接观察编译器生成的五个 guard：初始化一次、嵌套、
abort/retry、8 字节 guard 位于页尾、递归终止、无效读取、初始化途中故障及取消均
通过，共 12 任务、14 次精确回收，104 项输入未变，12 个日志反例拒绝。
首轮夹具错误要求 8 字节对齐，固定 Clang IA32 的实际 8 字节 guard 使用 4 字节
自然对齐；原失败保留，程序和提供者均未改，校正符号检查后通过。
当前完整 25 正从停止的 24 缓存副本准备；尚无实际新链接终态，不能先宣称 guard
首错已越过。Content 参考 08 的同一驱动仍存活并复制缓存，没有重启。完整 V8 ELF、
客体 Isolate/JS、浏览器、视频和完整 HTML5 仍未验收。
以下完整 24 是当前最新终态；后续状态以机器清单与实际构建终态为准。

完整 24 于 13:55:45（上海时间）最终链接返回 1，绑定输入未变；首错从 asprintf
前进到 __cxa_guard_acquire，真实消费者包括固定 libc++ locale.cpp 的 num_put
浮点输出和其他局部静态初始化。全部 5128 条原对象命令只出现 36 条必要的私有
include/errno 定义变化，新增两个分配式格式化对象；原有顺序链接输入和引擎参数
保留。十个实际对象与刚在客体执行的提供者逐字一致，GN/对象复审和 1260 份冻结
输入快照均通过。实际 Ninja 为 43 个任务、65.2 秒，缓存准备另计。
新的工作包是正确的 C++ ABI guard：核对布局、初始化发布、abort/retry、递归进入
及故障/取消的相关边界，随后重放同一目标。完整 ELF、Isolate/JS 和浏览器仍未通过。
以下完整 24 准备期和更早记录保留为历史证据，当前状态以机器清单的终态 24 为准。

分配式格式化工作包已在两个 QEMU 冷启动中通过 78 组 double/80-bit long double
对照（含 6000 位输出）、空/小输出、动态增长、错误/OOM 清理及故障/取消回收；
共 8 任务、10 次精确回收，368 项绑定输入未变，12 个日志反例拒绝。
保留原完整精度算法，私有工作区减少大整数临时副本并用真实堆保存大型转换器；
最大单函数帧为 2236 字节，所测路径在原 8176 字节用户栈下执行，没有扩容。
首次错误曾触发 LLVM errno 的 TLS 分配；原生堆也经该入口报告 OOM。当前强制
单用户任务的诊断改用固定 LLVM shared 模式，每个私有地址空间保存状态，不声明
多线程支持。非 Linux 通用 printf 分支将相关错误映射为 EDOM，原生 malloc OOM
仍为 ENOMEM；早先夹具的 EINVAL/ENOMEM 预期失败保留。
完整 24 正从停止后的 23 缓存副本准备，尚无新完整目标终态，asprintf 是否越过
仍待实际链接。以下完整 23 是当前最新终态，不能以客体叶子通过代替引擎运行。
Content 参考 07 最终因隔离副本丢失 metagen 包目录名而循环导入失败。包布局草稿
02 的真实完整生成动作已通过，输出 87159 字节实例类型头和 289852 字节布局文件；
参考 08 正从停止的 07 缓存副本准备。原 Chromium/V8 源码与全部失败记录保留；
生成动作通过仍不等于完整 Content 编译/链接或 GTOS 浏览器验收。

完整 23 于 2026-10-12 09:54:58（上海时间）最终链接返回 1，绑定输入未变；
首错从 toupper 前进到 asprintf，真实消费者是固定 libc++ locale.cpp 的
num_put::__do_put_floating_point<double/long double>。GN 仅新增一个 Ctype 目标和
两个对象，3175 条原命令、原链接输入及引擎参数保留；实际两对象与独立产物逐字
一致。实际 Ninja 7 个任务、77.1 秒，缓存准备另计，仍无完整 ELF 或客体 Isolate/JS。
Ctype 的两次 QEMU 冷启动覆盖 4 任务、2056 次转换、2080 项检查及 6 次精确回收；
全部 unsigned-byte/EOF 与独立宿主 C locale 表一致，12 个观察器反例拒绝。
Content 参考 07 准备期曾保留原源码，使用隔离 metagen
工具副本修正内部 GN 根入口。该工具已实际采集 207 项 IA32 参数，Clang 宏确认
i386/4 字节指针。查询成功不能当作完整 Content 编译/链接或 GTOS 网页证明。
GTOS Isolate、JS、Content 网页、视频及完整 HTML5 均未验收。
当前迭代按下文“紧接着执行的工作包”推进；以下较早构建记录作为历史证据保留。

本次只读校准再次核对 GitHub 按创建和更新时间排序的 issue、#9 正文与评论，
要求没有变化。该次校准时，asprintf/vasprintf 仅独立编译通过，long-double 工作区
仅准备完成；此后的实际客体验证与完整 24 进度以上方最新记录和机器清单为准。

当前 dev 与 origin/dev 为 45a00a8cb8e8ac04de3be9a42e7436980a0fb9ef，
zlib/FileStore 工作包已正常提交并推送，
[CI](https://github.com/awe0w0/GTOS/actions/runs/38164050213) 四项既有门禁均通过。
这些门禁不包含完整 V8 或浏览器运行。此前已提交基线 ff537182673747682718ff4e159f4ff78c6c563a
及其 [CI](https://github.com/awe0w0/GTOS/actions/runs/38139891840) 证据继续保留。
2026-10-12 再次按创建及更新时间核对 #9 和评论，路线要求未变。portable-cold-08
冷构建越过 zconf.h，暴露 zlib 的 pthread.h 和 gzip 文件接口缺口。随后实验的
zlib-platform-draft-03 选择上游完整标量编解码分支，将 gzip 文件操作接到既有
File ABI，复用已验收 errno 声明及真实 LLVM strerror 提供者。此前完整目标
zlib-draft-resume-11 已返回 1，绑定输入和 954 个文件的冻结快照一致，越过 zlib 后首个失败为
cctz/time_zone_libc.cc:121 的 tm_gmtoff 调用不匹配；同轮还报告 tm_zone 与
time_zone_info.cc:38 缺少 fcntl.h。该轮完整目标最远为编译，完整 ELF、链接及客体
引擎运行尚未通过。该增量的源码和执行资格已收束，随本工作包发布；gzip 客体语义通过的范围见下文。

cctz-native-resume-12 已取得终态，返回 1；首个失败为 time_zone_info.cc:663 的
basic_ifstream 未定义，全部绑定输入未变，完整依赖清单与 11 一致。
源码已经包含 fstream，诊断 SDK 的 _LIBCPP_HAS_FILESYSTEM=0 隐去了实现，实际消费者
是 Fuchsia 时区资源回退路径的 revision.txt 首行。独立草稿 02 改用已验收的 fgetc
和原来的 string/FilePtr，保留 NUL、高位字节、长首行、缺失或错误读取及关闭语义；
非 GTOS 分支和其余 time_zone_info 代码逐字保留。独立原生编译两个对象通过；GCC 13
的实际版本消费者对照十个用例通过，只有 fixture 前缀被替换，未验收 TZIF 解析或
GTOS 客体。初次对照被 GCC 9 的版本检查拦住，自审初版使用了错误锁字段；失败记录
保留，纠正后的自审通过，没有改源码锁或放宽断言。

cctz-native-resume-13 已从停止后的 12 缓存副本重放同一真实目标，GN 图、依赖清单、
链接命令和实际编译数据库生成通过；04:04:33（上海时间）取得终态，返回 1，绑定输入
未变。首个失败已前进到 builtins-effects-analyzer.cc:98:17，std::basic_ofstream<char>
未定义。新的 time_zone_info 已由
完整目标实际编译，两个原生时区对象与独立诊断逐字一致，依赖清单 SHA256 仍为
db417fe8c5b66ff9c5c6ca59a6343ad7a4b163fc382126ef601e23db4b1b0280。
这只证明 cctz 的该项编译阻塞已越过；FILE/env/C++ 未解析符号仍待真实链接，
完整 ELF 和客体 Isolate/JS 尚未通过。机器清单分别保存终态 12、13 与消费者
诊断及 Linux 对照。

失败源码第 7 行已经包含 fstream；实际 SDK 的 _LIBCPP_HAS_FILESYSTEM=0 使 libc++
fstream 第 194 行的实现条件不成立。补头文件不能解决该根因。Write 的已查调用位于
mksnapshot.cc:320，用于生成 builtins 效果源码；同时真实 V8 的 FileOutputStream
仍持有 ofstream。下一工作包先核对宿主生成与客体运行边界，再将真实所需文件流的
定义、运行库提供者及 FILE 语义接入同一完整 GN 目标。已有独立流实验继续复用，
仅打开宏不能证明链接或客体正确；保留失败对象及完整消费者，不以删对象获得通过。

独立 fstream-native-draft-01 已启用文件流定义，复用原生 fileno 句柄分支，并将固定
libc++ 的 ios.instantiations.cpp、ios.cpp、locale.cpp 接入完整 14 的最终链接目标。
GN 和实际命令生成通过：原依赖全部保留，仅新增 native_stream_libcxx；引擎参数与
13 逐字一致，V8 仍用 C++20，库源码按上游库规则使用 C++26，保留当前诊断的无异常/
RTTI 限制。编译数据库中的另一 libc++ 目标未进入此链接，排除了重复提供者疑点。
完整 14 已实际编译 builtins-effects-analyzer.cc，生成 ELF32/i386 可重定位对象，
SHA256 a8c110a58d567df8c735c61c59934f5b31919035dd5ae5d52721c1530337e2e0，
与独立原生命令的对象逐字一致；13 中该对象不存在。首个 ofstream 定义阻塞已在
真实完整目标中越过。首轮独立检查误将新旧完整 SDK 同时加入搜索路径，造成 errno
包装头跳过；失败记录保留，改为替换 SDK 路径后通过，完整构建始终只有一个 SDK。
完整 14 已于 04:36:34（上海时间）返回 1，绑定输入未变。首个失败为
sampler.cc:579 调用 make_unique<Sampler::PlatformData> 时平台类型不完整，
实际诊断在 libc++ unique_ptr.h:708 与 :72；没有完整 ELF。ofstream 定义阻塞已越过，
库提供者编译、完整链接和文件流客体语义仍须分别验收。
sampler-native-draft-02 的单文件原生命令及库目标 locale.cpp 已编译通过；956 个锁定
文件和源码/机器码复审也已通过，非 GTOS 分支可逐字还原。原复审按 objdump 显示名称
查找方法，未区分三个同名节；改按 ELF 符号节索引后确认三个方法分别只有 ud2，旧
失败记录保留。完整 15 已使用 14 的缓存副本启动，GN 依赖标签与 14 完全一致，引擎
参数和 sampler 编译命令未变，告警调整仅限三个 libc++ 库源码。实际完整目标已编译
sampler.cc，其对象 SHA256 为 45d88ea9340f7263a2870f9e00f860e8bbc60e1932387d13bae8bac440dfdc47，
与独立诊断逐字一致；类型错误已越过。完整 15 于 05:20:17（上海时间）返回 1，绑定
输入未变，首个失败前进到 utils.cc:8 缺少 sys/stat.h。完整 16 已越过该编译错误，
实际 utils 对象与独立原生命令逐字一致；全部目标编译后进入最终 ELF 链接。
05:34:07（上海时间）链接返回 1，绑定输入未变，首个错误为未定义 strcmp。
独立输出按原链接输入和 --error-limit=0 展开 150 个未定义符号，未忽略错误，未改
原缓存；首轮诊断因原失败链接未产生 map 而在重放前停止，失败与修正记录保留。
现有 native_string_libc、native_printf_libc 及其 native_c_clock 依赖已有生成命令，
却未进入最终目标依赖。23 个现有提供者按原命令编译全部通过；追加这些对象的独立
链接消除 17 个字符串、格式化和时钟缺口，剩余 133 个，无新增缺失符号。
runtime-link-draft-01 仅接入这两个已有 GN 目标，并绑定实际消费的 239 份依赖；
原函数、引擎参数、ABI、资源上限和原链接输入均保留。完整 17 从 16 的缓存副本
实际续编至最终链接：新增三个标签与 23 个提供者输入的 GN 接线复审通过，23 个
实际对象与独立原生命令产物逐字一致，strcmp 首错已被越过，新的首错是
std::uncaught_exceptions()。本轮默认错误上限仅展示前 20 个未定义符号；133 个
仍是第16轮追加提供者实验的计数，尚不能写成第17轮全部缺口数。
完整 ELF 和客体尚未验证。

固定 libc++ exception.cpp 在没有 libc++abi、libcxxrt、glibcxx 或微软运行库分支时，
选择 exception_fallback.ipp；其中 uncaught_exceptions 明确未实现并调用
__libcpp_verbose_abort。实际消费者使用 -fno-exceptions 并不移除 sentry 对该符号的
调用。因此简单增加 exception.cpp 会引入未实现分支，不能作为正确修复；下一包须
核对无异常配置、C++ ABI 与全部实际消费者需要的语义，再接入正确提供者。缺口还
包括 basic_string、__shared_count、__call_once、steady_clock、FILE 和数学符号，
应区分已有实现未接线与确实缺失的服务，不批量填入假成功占位。
草稿将未支持的
采样操作显式失败，不能据此宣称异步采样服务可用；若真实 Isolate 初始化依赖这些
操作，必须实现实际服务或证明上游支持的禁用配置，不得绕过失败。locale 的告警策略
仅限三个 libc++ 库源码，不能扩大到引擎消费者。

完整 13 还实测了重复重编的原因：builtins-string-gen 的实际命令相同，唯一较新
依赖 bytecodes-builtins-list.h 的内容逐字相同，但宿主生成器重写了时间戳。
独立 tools/run.py 草稿对实际生成器的六项检查通过，仅成功且完整输出相同才保留
原时间戳；首轮时间精度夹具失败及修正记录保留。草稿尚未进入完整目标或发布，
其采用须限定在下一独立输入包并验证实际 Ninja 增量行为，优先级低于文件流阻塞。

最新 zlib-guest-17 在 O0/32 MiB/单 vCPU 和 O2/64 MiB/四 vCPU 的 CPL3 读取并解码
12 KiB，验证校验值、seek、短读写、错误传播和句柄所有权。正常返回执行自动/TLS/fini
析构；非法访问、立即退出和取消路径跳过它们。两组均完成六个任务、16 个文件句柄
的精确回收，同伴、ring0 和启动任务继续推进，每组另有 204 个观察器负例。O2 的
16 个 zlib 对象与完整目标逐字一致，实验 FileStore 对象通过原整数指令审计。
实际 deflateInit2 的
小配置仍请求五块共 74164 字节，其中 65536 字节的哈希表分配失败，返回
Z_MEM_ERROR；已成功分配的四块全部释放，前后最大可分配块均为 53088 字节。
此前 zlib-guest-07 的压缩成功断言不符合实测容量，属于夹具预期错误；压缩成功
仍未验收，没有扩容或替换上游编解码。01–16 的失败日志均保留。

此前 mode 1 的截断 gzip 预期 Z_BUF_ERROR，实际 gzerror 为 Z_OK。
zlib-eof-reference-01 使用同一固定、未修改 zlib 的
Linux x64 标量代码，对同一缺少末尾四字节的文件，以 1024 和 1025 字节读取也均
返回 Z_OK、gzclose 为 0；CRC 损坏对照返回 Z_DATA_ERROR。固定 zlib.h 的契约要求
区分未完成 gzip 流。真实状态表明重复读取清除了错误，随后 EOF 快捷分支未恢复它。
草稿仅在 __GTOS__ 的两处 EOF 分支保留未完成 GZIP 的 Z_BUF_ERROR；完整流和透明
文件不进入该条件。zlib-eof-reference-02 验证了重复读取、seek、完整/透明/拼接流
及补齐尾部后 gzclearerr 的继续解码；Linux 原分支保留。

继续解码的客体负例又暴露 FileStore 已打开只读句柄的版本快照：写句柄成功关闭后，
旧读句柄仍见 11049 字节，新打开句柄见 11053 字节。FileStore draft-02 仅在确实
修改过的写句柄 Sync/Close 成功后刷新同一文件的只读缓存，保留读游标和所有权；
失败提交、未修改的旧写句柄关闭、其他文件及删除后替换的路径均不刷新。
zlib-guest-17 已验证补齐 gzip 尾部后的原流继续解码。host-02 对真实 FileStore 和
固定 LittleFS 完成 4201 项逻辑检查，包括 inline/CTZ 转换、游标、失败提交和身份
边界；这是 Linux x64 单线程逻辑对照。native-filesystem-01 已完成 O0/O2 的 174 次
实际 ATA 冷启动和 84 个替换写入扇区的中断恢复点，上游 LittleFS 互读、磁盘保护区
及原整数指令审计均通过；执行的 FileStore 对象与 zlib-guest-17 一致。
native-file-scalar-01 和 native-file-llvm-02 分别完成 O0/O2 的八组 File ABI 客体回归。
此前复制仓库的 LLVM 01 在旧 memset 对象哈希检查处停止；逐 ELF 节比较证明差异仅
为 UBSan 的来源路径字符串，所有可执行节一致。02 保留规范运行库路径，仅显式替换
实验 FileStore 内核源码，继续要求运行库与旧资格逐字一致，并核对 FileStore 与已执行
存储回归的对象一致；没有改写旧哈希或剥离对象。native-c-file-02 的 O0/O2 客体也已
通过，执行的 FileStore 对象与存储回归一致。跨工作区路径的重新构建资格尚未收束。
public-zlib-guest-01 已从公开草稿入口重新构建 50 个消费者对象，O0/O2 客体均通过；
public-zlib-guest-02 复用全部 50 个已锁定对象，两组客体和每组 204 个观察器负例也通过。
public-zlib-cache-negative-01 在编译及客体启动前遇到运行环境错误
`expected str, bytes or os.PathLike object, not NoneType`，并未到达预期的缓存篡改拒绝，
不能记录为负例通过。02 使用与已通过重放相同的工具环境，准确拒绝被修改的对象及其
伪造自报哈希，未编译或运行客体。full-kernel-01 的 O0/O2 正常内核构建及原整数指令
审计已通过；production-01 的两组 PNG 显示/关闭/重开/回收通过。新镜像已载入并重启
原 GTK 虚拟机，同一窗口 PID 752，私有磁盘字节不变。自审重新核对 16 份 File ABI
日志，1508 个观察器负例与十个真实服务 ISA 注入控制均被拒绝。
这次驱动问题与 cctz 服务缺口分别记录。已有工作包收束后立即返回完整目标，不能据
两组 gzip 客体通过宣称整个文件系统资格或浏览器完成。复用未改动的既有 FILE/VM/FP
对象，不扩展无消费者探针。

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
`tools/build-native-v8-integration.py`。当前模块的 84 个平台覆盖、426 个私有/C++ SDK 文件和
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
portable-resume-06 越过该对象，当时首个失败为 futex.h:31 缺少 unistd.h，随后
还报告 mutex.cc:65 缺少 fcntl.h。核对实际覆盖及完整编译参数后确认 GTOS 原已
排除 pthread、Linux futex 和 POSIX semaphore；此前基于上游文件的 pthread
判断有误，原分析及纠错证据均保留。修复两处未用 POSIX 头条件后，
portable-resume-07 的完整目标越过 futex waiter/mutex，首个失败前进到
zlib/zconf.h:505 缺少 unistd.h，同轮仍报告 cctz 的 tm_gmtoff/tm_zone 和时区
文件接口缺口。实际 waiter 选择标准 C++ 分支；条件等待目前只有 SDK 声明，
没有原生提供者。完整 ELF 仍未生成，单任务锁仍不是通用线程。
此前同图 attempt-07 还实际暴露了 seed_material
熵读取、futex 等待、时区文件读取及 tm_gmtoff/tm_zone 接口缺口。并行编译的首个
报错次序随缓存变化，保留每轮原始诊断，不将头文件声明当成服务实现。

上述 portable-resume-02 至 07 的完整 GN 依赖清单 SHA256 相同，未删失败对象。
后续 zlib 草稿选择上游标量分支，依赖清单随可选加速对象变化；完整编解码和 gzip
文件对象仍保留，不能继续沿用“依赖图未变”的表述。完整 ELF、链接与客体运行
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

content-shell-linux-ia32-compile-04 已对同一固定版本执行真实 ninja content_shell，
到达 598/45432 个任务后由构建驱动的 1200 秒限时停止（exit 124）。绑定输入未变，
尚无编译错误诊断或 Content Shell 可执行文件；其中包含宿主工具和 IA32 生成任务，
不能把计数当成目标对象数或整体完成比例。该结果是超时，不能证明 IA32 不可行，
也不能证明编译/链接通过。compile-05 同参数 GN 通过，实际 Ninja 到达 9377/44834
后由驱动的 10800 秒限时停止，返回 124，无编译诊断、无可执行文件，绑定输入未变。
确认实际终态后，compile-06 从完整缓存副本以相同版本、参数及四个任务续编，
限时延长为 86400 秒；06 已通过 GN，实际 Ninja content_shell 正在运行，尚无终态
或可执行文件。
旧缓存及日志保留，不因观察超时重复启动。此前参考
记录的 args 校验字段过期；实际参数、准确 GN 命令及 args.gn 三者一致，04 已记录
实际 SHA256 e7e3e0e8298fd8091b30bfac22b71ebaa1512013ba16652bc76bd13be0930a9d，
原记录保留，不能把旧字段当成新构建输入。

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


完整 17 首个 C++ 阻塞的独立草稿 cpp-runtime-draft-01 已加入受编译保护的无异常
提供者：uncaught_exceptions 返回零，仅适用于明确禁用 C++ 异常展开的诊断配置；
__throw_bad_alloc 调用既有原生堆的 OOM 终止路径，不返回成功。新目标同时接入固定、
未修改的 libc++ string.cpp 与 memory.cpp。1053 条实际 native_embedder 原生 C++
命令均关闭异常；未选中的 Torque language server 不属于此契约。原编译数据库
覆盖更大范围，首版把未选中目标计入并在编译前停止，失败记录保留。
第二次实验发现新目标缺少 runtime/sdk 搜索路径，修正后第三次三个对象均编译通过，
异常启用、诊断宏未定义及值为零的三个配置负例均由明确保护拒绝。ELF 符号节自审
确认异常计数函数只有返回零的机器码，bad_alloc 调用真实 heap panic。
958 个草稿文件及实际消费依赖已绑定；未增加异常展开器、线程能力或资源上限。
完整 18 已从 17 的缓存副本取得终态：06:32:07（上海时间）实际最终链接返回 1，
全部绑定输入未变。GN 仅增加 native_cpp_libcxx 标签和三个提供者对象，3144 条原有
原生命令、原链接输入及引擎参数均保留；新三个对象与独立产物逐字一致。实际目标的
1056 条 C++ 命令均关闭异常。首错已从 uncaught_exceptions 前进到 fopen；setbuf、
FILE、verbose_abort、数学、__next_prime 和 __call_once 等缺口仍存在。默认错误上限
只展示前 20 个未定义符号，尚未测量这一轮全部缺口数。没有完整 ELF 或客体引擎，
string/shared_ptr 等运行行为仍须在真实客体中验收；不将链接增量当作运行资格。


file-runtime-draft-01 已按锁定哈希导入已验收的 LLVM File 核心、21 个字节流入口
和原生后端，另接入固定且未修改的 setbuf 源码。setbuf 复用已验证的 set_buffer：
空缓冲选择 _IONBF，调用者缓冲选择 _IOFBF/BUFSIZ，错误写入 errno。源代码、
所有者查询及 File ABI 头文件均逐字保留；1250 个输入和 190 份消费依赖已核对。
25 个提供者按同一工具链/SDK 编译通过；完整 19 的真实 GN 仅增加 native_file_libc
标签及 25 个输入，明确使用平台锁、TLS errno 和优先的已适配 FILE 头文件。
07:04:40（上海时间）实际最终链接返回 1，输入未变；25 个实际对象与独立产物逐字
一致，fopen、setbuf、文件读写/seek/close 首阻塞已越过。下一首错为 libc++ 的
__libcpp_verbose_abort；其默认实现要求 vfprintf(stderr) 后终止，需要正确原生输出
与终止语义。标准流仍未实现，旧空指针提供者没有导入；不把空 stdout/stderr 当作
可用标准 I/O。C++、数学和 OS::FOpen 等其余真实依赖仍未解析，默认错误上限仅显示
前 20 个缺口，尚未测量全部计数。新的 setbuf/文件流客体行为仍未验收，完整 ELF
与客体引擎均未通过。准备工具首版误读 LLVM pin 的字段名，在复制前停止；原失败
保留，按真实仓库路径键修正后通过，没有修改 pin 或放宽哈希验证。


diagnostic-runtime-draft-01 通过固定 LLVM callback 格式化器，把完整消息分块送入
真实 GTOS_SYS_WRITE，每次最多 256 字节；保留短写推进，零返回或错误返回失败，
失败后不重复刷新已经写出的前缀。格式转换失败时尝试刷新此前有效内容，终止钩子
无论输出结果如何都调用既有原生 abort。没有借用空 FILE 标准流，也没有把消息
限制为固定缓冲长度。两个对象编译通过，诊断关闭/零值和伪装 Linux 的三个负例
明确拒绝；初版身份保护晚于头文件，伪装 Linux 先在 linux/errno.h 失败，原记录
保留，保护移到头文件前后重放通过。1253 份输入、186 份消费依赖已绑定，源码/
机器码自审通过；两函数自身帧分别为 308 和 4 字节，原 long-double 大帧问题保留。
完整 20 已实际消费两个逐字一致的对象，GN 仅增加 native_diagnostic_libcxx；
07:36:15（上海时间）最终链接返回 1，输入未变，首错已前进到 __call_once。
源码位于固定 libc++ call_once.cpp，当前 _LIBCPP_HAS_THREADS=1 路径使用锁、
条件等待/广播及 release 发布；它们的真实服务、回调恰好一次、失败状态恢复和
pending/reentry 边界需核对，不能返回假成功。标准流、C++/数学/平台符号仍缺，
默认错误上限仅显示前 20 个，尚未测量全部计数。新诊断输出、终止及回收的客体
资格仍未通过，完整 ELF 和客体引擎未运行；草稿未发布。


once-runtime-draft-01 依据完整 20 的实际首错，提供显式 IA32、无异常、单用户线程
诊断分支。它先核对内核报告的线程/调度能力，再读取初始化状态；保留 unset=0、
pending=1、complete=0xffffffff，回调完成后 release 发布，其他 flag 的嵌套不持有
全局锁。同一 flag 的 pending/reentry、损坏状态或未开始时的空回调明确故障，
不伪造等待成功；全局线程 ABI 和非 GTOS 分支保留。独立编译及四个配置保护负例
通过，自身帧为 76 字节；实际完整 21 已消费同一对象，输入/命令自审通过。
日志同时显示宿主 call_once、libc++、Torque 和快照依赖重建，构建继续运行，保留
现场与缓存。随后将完整 21 的实际对象直接链接至当前 dev 的 CPL3 回归；
32MiB/1 CPU 与 64MiB/4 配置 CPU 两次 QEMU 冷启动，共 16 任务、18 次精确 Reap
通过。固定 libc++ 的真实 std::call_once 模板验证重复调用及不同 flag 嵌套，原生
提供者验证 pending/reentry、损坏状态、空回调拒绝、回调页故障和活回调取消。
每个任务持有一个真实 VM 页，故障后数据保留至 Reap；同伴、ring0 和 boot 继续
运行，最终页面基线恢复。241 份输入前后未变，12 个观察器篡改反例均拒绝。
首轮测试代码被 C++20 volatile 自增告警拒绝，修正为显式读写后在新目录执行，
旧失败保留，实际提供者和生产源码未改。资格仅为 QEMU/O2/显式单任务诊断；
Bochs、真实多线程等待及完整 V8/JS 尚未验收，草稿未发布。待完整终态确认下一首错。


诊断输出/终止另有两次 QEMU 冷启动、8 任务和 10 次精确回收通过：完整 20 的
八个真实依赖对象运行于当前 dev CPL3，949 字节整数/字符串消息未截断，空消息
返回 0，长消息和空消息的 verbose abort 均输出既有 25 字节 panic 并以
0x48000021 退出；同伴继续运行，VM 页保留至 Reap 后恢复基线。118 份输入及
八个对象未变，12 个观察器篡改反例被拒绝。资格仅为该整数/字符串/终止路径；
通用浮点、短写/错误写执行及 8364 字节长浮点帧仍未验收。测试初版数组初始化
告警和 once 自审初版错误假定无重定位均保留；后者改为严格比较实际本地常量
重定位和全部分配节，未修改提供者实现或放宽运行断言。

Linux IA32 Content Shell 参考 06 于 08:52:02（上海时间）返回 1，输入未变，
失败为 V8 metagen 生成器内部 GN desc。它仅复制 args 后使用默认 Chromium 根
入口，触发 BUILD.gn:1545 的 Linux x86 限制；原错误包装只附 stderr，隐藏了
stdout 的实际断言。独立新目录重放默认入口失败，指定被查询的 V8 目标为根入口
后成功。原参考源码与缓存保留；该查询修正不等于 Content 编译/链接或 GTOS
浏览器通过，后续需独立绑定适配再续编，IA32 当前交付选择保持。

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

zlib/FileStore 已在 45a00a8 发布；公开重放、相关客体回归、交付镜像桌面验证和
原虚拟机更新已完成。历史失败、旧镜像及原始缓存保留，压缩成功与完整引擎仍未验收。
该工作包退出待办主线，已有 FILE、VM、FP 对象按锁定哈希复用。

1. 完整34已实际越过StdcppWaiter的mutex/condition_variable，当前首错为
   `v8::base::OS::PrintError`，消费者包括V8的OOM与API失败诊断。同步库12个任务、
   648项检查及14次精确回收已通过，380个日志反例被拒绝。接着只读核对固定OS
   实现和已验收诊断输出、FILE/格式化提供者，补齐实际错误输出语义，必要回归后
   立即回放同一目标。5150条旧对象命令、引擎参数和旧链接输入顺序保留；
   已验收locale、guard、格式化、errno、FILE和同步库继续按哈希复用。
   Ctype 已在实际完整链接和 QEMU 全字节/EOF/回收中通过，保留锁与证据，避免
   重复无关矩阵。下一适配完成必要客体验证后重放同一目标，尽快进入真实 ELF
   装载、Isolate/JS 和精确回收。其余标准流、C++/locale/数学/平台缺口按实测推进。
2. 保持第 09 次隔离参考构建，从停止的 08 缓存副本续编同一 Chromium revision 的 Content Shell IA32 目标。
   第 08 次的缺失宿主 i386 加载器已定位，真实生成动作在显式加载器下通过；
   获得实际编译/链接结果与资源观测；超时单独记录，不冒充架构或 OS 失败。捕获
   第一个真实阻塞后及时校核路线，不能只凭指定根 GN 成功判断可行。若证据要求
   改架构，提交决策与代价说明，由用户决定；当前仍为 IA32。
3. 每次必要适配后重放完整目标，依据实际链接符号复用 C FILE/libc++；确定性种子
   不等于系统熵，诊断单任务锁不等于原生线程或 futex。冷构建和缓存续编分别记录。
4. 完整 ELF 生成后记录段、映像页、映射、栈与堆需求，再联动处理装载/容量；以
   客体 Platform → Isolate → Context → Script 校验与回收作为首项功能验收。

本次再次读取最新创建、更新的 issue 及评论，#9 仍为最新且无新评论。当前最远阶段
是完整目标实际尝试最终 ELF32 链接，并因未定义 C++/平台提供者失败；尚无可执行 ELF
或客体执行。每个首阻塞工作包
以同一完整目标的增量作为退出条件；后续顺序为客体 JS、本地 HTML/CSS 首帧与 DOM
输入，再受控 HTTP/HTTPS，随后视频和更完整 HTML5。IA32 的架构审查以实际编译/
链接、地址空间和装载需求为门槛；失败原因或代价要求转换时先提交证据与 x64 差额，
由用户决定，不无限增加 IA32 诊断模块，也不因单个可修复的库错误直接改架构。

每个工作包完成必要适配和相关边界验证后，立即重放完整目标并记录下一阻塞。
已有 FILE、VM、FP 等组件按源码和对象哈希复用；未改变其实现时不重复建立整套
独立资格矩阵。编译通过的声明不等于运行服务。条件等待已在受检查的单用户任务
配置接入真实时钟和调度让出，超时、取消和错误恢复均在客体验证；它仍不是共享
地址空间线程及跨线程唤醒。Content Shell 的实际编译/链接可行性仍是 P0
未关闭项，需保留证据并及时重审 IA32 方案；用户的 32 位目标保持有效。

PNG 桌面、文件 ABI 和 C FILE 成果继续复用；生产挂载与磁盘 PNG 接线已有交付镜像
验证，完整 V8/Content 的资源路径与文件流接入仍待完成。暂缓无主线消费者的 API
扩展、桌面美化和一般硬件专项。视频、GPU 与更完整 HTML5 的专项实现保留在本地
网页、DOM 输入和受控网络里程碑之后，用户的最终交付目标保持有效。

## 迭代报告与回归

每次报告：最远真实阶段；当前首个客观阻塞和准确命令/日志；修复使目标前进到哪里。
新增服务须注明实际消费符号、语义与 guest 证据。构建驱动失败、超时和主动中断应单独
记录，不能伪装为 OS 根因，也不能跳过已知失败对象后报告完整构建成功。

模块修改运行必要正反例与对应真实集成路径；子系统修改触发隔离、VM、线程/FP、
存储中断及回收专项；阶段性运行完整历史回归。保留旧 WIP、缓存、失败日志与可见 VM
磁盘。生产镜像更新时刷新并重启可见 VM。自审、必要测试后正常提交和推送 dev，
不强推；只报告确实运行的 CI。


## 2026-10-13 原生格式化FILE输出完成

固定V8六项输出包装及LLVM generic printf族接入同一完整目标；补齐真实FILE_SYNC
与截断打开提交。12个CPL3任务、12次精确回收、428个日志反例及两盘独立stock
冷读回通过，常规FILE/PNG、长文本、错误恢复、OOM和取消回收均保留验证。
公有源码与1287项载荷文件核对字节，未重跑公有GN或客体。实际完整链接full-link-37
保持5160条对象命令、引擎参数及顺序，首错现为ceilf。full-link-36是驱动计数错误，
未执行链接，不列作OS根因。完整ELF/Isolate/JS、浏览器、视频/HTML5仍未验收。
下一工作包由真实ceilf消费者及固定LLVM数学后端驱动，必要边界验证后立即重放
完整目标；不改变IA32路线或引擎功能配置。生产镜像和可见VM磁盘保持原样。


## 2026-10-13 原生基础数学完成

五项固定LLVM数学提供者已通过12个CPL3任务、12次精确回收、每任务921项数学
检查及444个日志反例；FILE/PNG和独立两盘冷读回通过。目标局部-fmath-errno
恢复库语义，5160条旧对象命令、引擎参数及旧链接顺序保留，新增五个实际对象。
完整38已实际链接，下一阻塞及日志以chromium-integration-status.json为准。
接着按当前真实消费者推进，必要边界回归后重放同一目标。公开字节对应已验证，
未重跑公开GN/客体或新远程CI。完整ELF/Isolate/JS、浏览器、视频/HTML5仍未
验收，用户指定IA32路线保持；生产镜像本批未变。


## 2026-10-14 原生退出登记与生命周期完成

真实析构登记和正常退出已通过14个CPL3任务、14次精确回收、7660项检查和
536个日志反例。成功关闭验证完整字节；失败Flush验证实际错误、保留退出码
及资源回收，不承诺新数据提交。完整39重放新增两对象并替换真实入口，保留
5165条旧命令及顺序。当前首阻塞以status记录为准，接着按实测推进同一完整
目标。公开字节对应已验证，未重跑公开GN/客体、远程CI或本批cold stock。
完整ELF/Isolate/JS、浏览器、视频及HTML5仍待验收；IA32路线及生产镜像保持。


## 2026-10-14 原生哈希、质数与排序

固定libc++三个真实提供者按原库配置接入，5167条旧对象命令和输入顺序保持。
八模式消费者检验独立预期、页边界、只读写故障、STL内存不足、取消和恢复。
两组共16任务、16次精确回收及必要日志反例后重放完整40，下一阻塞按status
实测记录推进。公开字节对应不等于公开GN/客体、远程CI或stock重跑；完整
ELF/Isolate/JS、浏览器、视频及HTML5仍待验收，生产镜像保持。


## 2026-10-14 原生浮点文本转换

三个固定 LLVM libc 提供者保留原库参数接入；实测十六进制向下舍入错误
已最小修正，同一共享头的旧 sscanf 重新编译并运行 41 项旧回归。
独立精确位模式、8 种对齐、解析终点/errno、页边界、四种模式和 FP 上下文
通过两组 16 个任务及精确回收；随后重放完整41，下一首错按 status 实测推进。
5170 条旧命令及最终顺序保持，新增3对象并更新1个共享头消费者。
公开字节对应不等于公开 GN/客体、远程 CI 或 stock 冷读回重跑；完整
ELF/Isolate/JS、浏览器、视频及 HTML5 仍待验收，生产镜像本批未变。


## 2026-10-14 原生 nearbyint

完整41的真实Atomics.pause检查驱动接入固定LLVM函数，IA32/SSE2直接读取
舍入位并复用原整数舍入算法。两个早期准备失败保留；原提供者环境种子
对照和局部适配均通过，未宣称客体已观察到generic新inexact副作用。
32独立向量、4模式/2标志初态、跨timer/syscall和两个真实标志破坏负控，
两配置12任务/12精确回收后重放完整42。旧5173命令/顺序保持，新增1对象。
下一首错按status实测推进；公开字节对应不代表公开GN/客体、远程CI或
新stock冷读回。完整V8/JS、浏览器网页、视频及HTML5仍待验收。

## 2026-10-14 原生虚函数 ABI 终止

完整42首错驱动接入固定pure/deleted实现，旧5174命令和次序保持。两个真实
ABI经实际诊断/abort退出，16任务精确回收。12次真实写后的返回值故障按固定
非Linux格式器映射校验EDOM33，并证明不重试已写出的诊断。完整43首错以
status实测推进；公开字节对应不代表公开GN/客体、远程CI或新stock读回。
完整V8/JS、浏览器网页、视频、HTML5仍待验收，生产镜像未改。

## 2026-10-14 原生 V8 FOpen

完整43首错驱动接入真实普通文件桥接，旧5175命令和次序保持。8任务精确
回收，1036检查和314日志反例通过；目录/路径/mode/排他/OOM，以及活跃
FILE取消/故障走实际原生通路。旧remove清理依赖失败保留，不改桥接库。
完整44首错以status实测推进；公开字节对应不代表公开GN/客体、远程CI或stock读回。
完整V8/JS、浏览器网页、视频、HTML5仍待验收，生产镜像未改。

## 2026-10-14 原生每线程存储与析构回收

真实 emutls/errno/析构在两配置74上下文、75816检查和精确回收后验收；
8轮worker重建、模板/对齐/32变量增长、析构再注册、取消、TLS OOM与页故障
均走实际CPL3通路，两种错误共享存储负例拒绝。默认FOpen另过8任务回归。
新GN保留5173命令及输入顺序，新增context，三个clock/errno配置重编。
公开源码与私有验收逐字节对应，不等于公开GN/客体或远程CI重跑。
共享mutex/condvar/call_once/guard仍有单任务限制，下一步实际适配这些调用，
再接入固定V8 Thread/Semaphore。完整45未重放，原生浏览器/视频/HTML5
未验收，生产镜像本批未改。


## 2026-10-14 真实 V8 线程桥接

固定 Thread/Semaphore 与每线程 key 在 GTOS 通过76上下文精确回收、48次Join、
默认栈和key代际复用；默认单线程失败路径也通过。FOpen stripped ELF 与既有
验收版本相同，沿用此前运行证据。新GN保留5177旧命令和最终顺序。
完整45已按真实38成员归档重放，首错为 CreateTimezoneCache，完整ELF仍缺失。
详见[原生V8线程模块](../apps/native_v8_thread/README.md)。生产镜像未改，完整
Isolate、浏览器、视频和HTML5仍未验收。


## 2026-10-14 V8 平台核心补齐

UTC-only时区缓存、真实有界格式化及固定调度钩子在GTOS通过76上下文、91854
检查和精确回收。FOpen字节与既有验收版本相同，沿用旧运行证据。GN图未变。
完整46按真实38成员归档重放，三项缺失符号消除，首错变为isspace，ELF仍缺失。
详见[原生平台核心模块](../apps/native_v8_platform_core/README.md)。生产镜像未改，完整
Isolate、浏览器、视频和HTML5仍未验收。


## 2026-10-14 字符分类与整数解析

固定 LLVM 的 isspace、isprint、strtol、strtoll 已接入原有运行库配置，函数体
未改。GN 新增四个对象，原5179条命令和最终顺序保持；客机76上下文、145358
项检查、48次Join及精确资源回收通过。完整47消除这四项缺失符号，首错变为
SetDataReadOnly，诊断仍非穷尽。详见[原生分类与整数模块](../apps/native_text_integer/README.md)。
完整ELF、Isolate、浏览器、视频和HTML5尚未验收，生产镜像未改。


## 2026-10-14 静态数据只读保护

独立版本的原生只读ABI与OS::SetDataReadOnly已接入。真实FlagList::FreezeFlags
客机20上下文、532项检查、整范围拒绝、共享线程PF7、自覆盖请求和精确回收通过。
完整48消除SetDataReadOnly缺失，首错exp2，诊断未穷尽。详见
[原生静态只读模块](../apps/native_static_readonly/README.md)。现有GN图不变，生产镜像未改；
完整ELF、Isolate、浏览器、视频和HTML5尚未验收。

## 2026-10-14 exp2 数值提供者

固定LLVM exp2及GTOS IA32极小输入舍入适配已验收：两组默认完整整数幂、
各线程全部58边界/四模式/两种种子，52048次调用、76上下文精确回收。
真实完整49首错前进到std::__gtos_isolate_diagnostic::cout，诊断未穷尽。
详见[原生exp2模块](../apps/native_exp2/README.md)。生产镜像未改，完整ELF、
Isolate、网页、浏览器、视频和HTML5仍未完成。

## 2026-10-15 原生标准流与四上下文同步

固定 libc++ iostream、真实宽字符 File 方法及严格 UTF-8 转换已验收。
默认标准流 8 进程/8 回收；四上下文标准流 32 上下文/24 Join/8 Reap；
同步回归 12 程序/648 检查/14 回收，once 回归 16 程序/18 回收。
62 个修改头使用者及两处宏选头已审查，完整53输入和缓存终验通过，
但实际链接仍缺 OS::GetLastError 等依赖，完整 ELF 和浏览器尚未运行。
详见[原生标准流模块](../apps/native_iostream/README.md)。四上下文为 BSP 范围，
未宣称用户态 SMP；生产镜像未改，视频和 HTML5 尚未完成。

增量 UTF-8 自审补充：非法首字节和第二字节立即 EILSEQ；8 个非法前缀、
6 个合法逐字节边界及原有生命周期/资源回收在最终 16 个标准流场景通过。
旧对象 C0 负控制在两配置精确失败，修正了把非法前缀报告为未完成的回归。
完整54保持 GN 顺序，7724 项输入和旧缓存终验通过；仍首缺 GetLastError，
完整 V8、网页浏览、视频与 HTML5 尚未运行。


## 2026-10-15 原生 OS 错误与临时文件

GetLastError、LogFileOpenMode 和 OpenTemporaryFile 已接入真实每线程 errno、
w+ 文件和立即 unlink 临时文件。默认与 BSP 四上下文控制共 24 程序、48 上下文、
24 Join、24 次 Reap；覆盖实际 ENOMEM/EMFILE、只读卷、非法标志、磁盘错误、
UD2 故障及外部取消，资源精确恢复。详见 [原生临时文件模块](../apps/native_os_file/README.md)。
完整55保持实际 GN 顺序，仅替换两个已有提供者；三项 OS 缺失已消除。
完整 V8 ELF、Isolate、网页浏览、视频和 HTML5 仍未运行；生产镜像未更新。
当前完整链接首缺：v8::base::OS::GetSharedLibraryAddresses()；错误数量上限下的诊断尚未穷尽依赖。

## 2026-10-15 原生平台辅助接口

共享库枚举、外部 GC 标记、目录分隔符、16字节栈对齐查询、真实 UTC 毫秒时间
和有界 StrNCpy 已验收。默认与 BSP 四上下文控制共44程序、68上下文、24 Join、
44次 Reap；20个非法复制程序精确停止且目标未变。详见[平台辅助模块](../apps/native_platform_helpers/README.md)。
完整56保留实际 GN 顺序和38成员归档，只替换平台成员，其余37成员逐字节保持。
完整 V8、Isolate、浏览器、视频和 HTML5 尚未运行；生产镜像未改。
当前完整链接首缺：ldexp；本次链接诊断已完整返回。

## 2026-10-15 原生 libc 缺口

九项真实数值、字符串、字符和模块化 printf 提供者已验收，详见[运行库模块](../apps/native_runtime_gap/README.md)。
保留 pow 准确路径和实际 FILE 输出；ldexp 明确采用 GTOS 四种舍入模式统一范围错误约定。
默认与 BSP 四上下文、两种内存/CPU配置合计44程序、68上下文、24 Join和44次Reap。
原有 FILE、C++并发输出、同步与退出回收回归保留；生产镜像未改。
实际 GN 原5187项命令保留，仅增9项；旧1101个链接输入相对顺序和38成员归档保持。
完整 V8/Isolate、浏览器、视频和 HTML5 尚未运行。本机执行继续遵守固定脚本、Python
-I -B -S、无网络及限定写入目录的栅栏；公开 canonical 构建入口未在本轮重跑。
完整57实际首缺：v8::base::debug::EnableInProcessStackDumping()；本次链接诊断已完整返回。
