# 32 位 Chromium 原生集成主线

2026-10-11 对照 [issue #9](https://github.com/awe0w0/GTOS/issues/9) 与本地构建证据校准。
最终目标是在 GTOS 客体执行真实 Chromium，显示并操作网页，再逐步验收网络、视频和
HTML5。当前交付架构为 IA32；x64 的内核态启动、帧池与稀疏 VM 是保留的备选证据。

## 当前证据与限制

代码基线为 dev 1153b561712d4b3e142a75291de5abefe6622a69。其 CI 覆盖已有门禁，
不包含完整 V8 或浏览器运行。Linux 参考引擎运行已完成，GTOS 客体 Isolate、
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

当前工作区已建立真正的 native_embedder executable，依赖上游 V8 与 libplatform，
包含原生入口、构造/析构数组以及真实 Platform/Isolate/Context/Script 调用。
GN 图与实际 ELF 链接命令已生成。完整 Ninja 首轮因 30 分钟时限退出，独立缓存
续编后实际失败，首个诊断是 Abseil elf_mem_image.h:48 缺少 elf.h，随后还有 pthread.h
与 unistd.h 缺失。完整 ELF 尚未生成，链接与客体运行均未通过。
同图单独编译的原生入口已经通过，不能由此推断完整 ELF 可装载或执行。

V8 的 v8_abseil 依赖整个 absl_component_deps，带入信号处理、日志及动态 ELF/VDSO
支持。elf_mem_image.h 按 __ELF__ 选择动态符号能力，vdso_support.h 随之启用 VDSO，
实现会读取 /proc/self/auxv 并调用系统服务。GTOS 的静态 ELF32 装载证明尚不提供
这些动态设施。下一步须核对真实消费者和平台能力边界，区分必要 SDK 声明、真实
服务与不适用的默认路径；不能只补空头文件或伪装 Linux。完整目标的这个首个失败
优先于下述单独文件流实验。机器清单见 chromium-integration-status.json。

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
attempt-04 保留完整目标超时，attempt-05 保留续编的首个真实失败；
direct-dependencies-01 是同图原生依赖诊断；
fstream-config-diagnostic-01 和 stream-runtime-diagnostic-03 是独立配置/流实验；
content-shell-linux-ia32-01 与 content-shell-target-evidence-02 分别保留根断言和目标图证据。
这些工作区路径不是干净检出的构建入口；发布可移植补丁、SDK 锁和重放工具仍属于 P0。

## 阶段与验收

| 阶段 | 工作 | 完成证据 |
| --- | --- | --- |
| P0 | 固定架构、上游源码/工具链、完整 embedder 与 Content 图；发布补丁和失败清单 | 干净目录可重放完整目标并稳定复现首个失败；当前尚未完成 |
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

## 迭代报告与回归

每次报告：最远真实阶段；当前首个客观阻塞和准确命令/日志；修复使目标前进到哪里。
新增服务须注明实际消费符号、语义与 guest 证据。构建驱动失败、超时和主动中断应单独
记录，不能伪装为 OS 根因，也不能跳过已知失败对象后报告完整构建成功。

模块修改运行必要正反例与对应真实集成路径；子系统修改触发隔离、VM、线程/FP、
存储中断及回收专项；阶段性运行完整历史回归。保留旧 WIP、缓存、失败日志与可见 VM
磁盘。生产镜像更新时刷新并重启可见 VM。自审、必要测试后正常提交和推送 dev，
不强推；只报告确实运行的 CI。
