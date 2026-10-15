# GTOS

简体中文与 English 可在外观设置中切换，语言和主题会保存在专用磁盘镜像中。
启动器支持按字符编辑 UTF-8 文本和有范围限制的拼音候选词；这还不是通用输入法。
详见[语言支持范围](docs/desktop-localization.md)。

GTOS 是一个在 `dev` 分支开发的 32 位 x86 教学操作系统。
当前基础阶段已实现真实的 800×600 窗口桌面、硬件监视器、经过验证的内存分配器、
内核页级保护、有界多核内核工作线程、定时器驱动的 BSP 调度器、安全的专用 ATA
应用存储，以及独立打包、可以游玩的 Catch 游戏。

项目仍在持续开发，尚不适合作为生产操作系统使用。当前桌面支持移动和调整窗口大小、
搜索启动器、最小化与恢复、外观设置和应用安装，并保留旧版 320×200 显示回退模式。
有界原生 ELF32 进程现在具有独立的 CPL3 地址空间、经过检查的系统调用和可恢复的
用户态故障。通用 AP 调度、完整文件系统/POSIX 运行时、持久化会话和更丰富的桌面服务
仍是路线图中的待办工作。
详见[工程路线图](docs/ROADMAP.md)。

真实的[原生 IA32 V8](apps/native_v8_launch/README.md)已在 GTOS 用户态完成
Isolate、Context、脚本编译与执行及退出回收；两种配置检查得到 `GTOS42` 和 `3.75`。
同一引擎的[工作负载补验](apps/native_v8_launch/workloads/README.md)继续覆盖对象/数组、
TypedArray/DataView、真实 GC、Promise 微任务及异常后的脚本恢复。
当前是单用户线程、jitless 入口。下一步推进 Chromium Content 本地首帧与 DOM，
网页浏览、视频和 HTML5 尚未验收。

Content 依赖所需的[GTOS Rust 目标基础](apps/native_rust_target/README.md)已固定工具链、
目标规格及真实 core 标准库编译证据；[真实 GN core 规则](apps/native_rust_target/gn/README.md)
也已按 O3 参数编译通过。[IA32 共享链接规则](apps/native_shared_link/README.md)已生成并检查
真实 ELF32 共享库和模块，包含 PIC、符号与拒绝边界。Content 图已推进到 libaom 的
NASM 平台配置；完整 Rust std、动态加载和 Content 构建仍待接入。

显式测试配置新增[有界原生线程生命周期](apps/native_thread_lifecycle/README.md)：
在 BSP 上调度共享地址空间的独立 CPL3 上下文，并验证退出、join、FP 和资源回收。
默认启动及完整 V8 入口仍关闭该能力；TLS、errno、共享锁和 V8 线程桥接的显式
BSP 多上下文验收见各原生模块，不构成通用 SMP 或浏览器多进程支持。

## 构建与运行

需要支持 i386 独立环境的 GNU g++/binutils、GRUB i386-pc 工具、
xorriso、mtools、Python 3 和 QEMU x86。无需 C++ 标准库或 multilib libc。

```sh
make GTOS.iso
./tools/run-qemu.sh
```

运行脚本会在 `data/apps.img` 不存在时创建一个新的专用 8 MiB 应用存储镜像，
并在多次运行和构建清理之间保留它。脚本不会挂载主机块设备。
默认启动流程不格式化磁盘。这个实验性应用包格式不是 FAT/ext4，
不可将其指向有价值的镜像或物理磁盘。

在无图形显示的环境中运行：

```sh
QEMU_DISPLAY=none ./tools/run-qemu.sh
```

可通过标准输入使用 QMP。收到初始欢迎消息后，可发送以下示例命令：

```json
{"execute":"qmp_capabilities"}
{"execute":"human-monitor-command","arguments":{"command-line":"sendkey i 100"}}
{"execute":"screendump","arguments":{"filename":"/absolute/path/desktop.ppm"}}
{"execute":"quit"}
```

脚本支持用 `GTOS_LEGACY=1` 启用 VGA 回退模式，也支持 `GTOS_MEMORY=32M`、
`GTOS_CPUS=4`、`GTOS_DISK=/path/to/image`，
以及可选的同级目录 `gtos-runtime` 无 root 权限工具链。必须从 ISO 启动；
不支持用 QEMU 的 `-kernel` 参数直接进行 Multiboot 启动。

## 桌面与游戏

- 点击窗口可使其获得焦点；拖动标题栏可移动窗口，拖动右下角可调整大小，也可使用标题栏控件
- `1`/`H`：主页；`2`/`M`：硬件监视器；`3`：应用；`4`：外观
- `L`：搜索启动器；`Tab`：切换窗口；`[`：最小化；`]`：最大化或恢复
- 在外观设置中选择 English/简体中文，或按 `C`；语言和主题会持久保存
- 中文启动器：输入拼音，按 1–9 或空格/回车选择候选词；反引号切换直接输入
- `I`：从启动 ISO 安装独立的 Catch 应用包
- `Up`/`Down`：选择已安装应用；`Enter`/`G`：启动
- `U`：请求移除所选应用；回车确认，Esc 取消
- Catch 游戏中使用方向键或 `A`/`D` 移动挡板，接住下落方块得分
- `R` 或空格：重新开始；`Esc` 或窗口关闭按钮：返回应用列表

应用安装和移除会在重启后保留。游戏逻辑是由 `apps/catch.json` 构建的
`apps/catch.gtapp` 字节码，并未内置到内核中。
详见[应用协议](docs/apps.md)和[存储格式](docs/storage.md)。

## 原生用户态基础

启动 ISO 还包含 `apps/native/` 中两个独立编译的 ELF32 程序，
以及应用侧的 `apps/browser_probe/` ABI 测试程序。它们在 CPL3 下运行，使用不同的
页目录，在相同虚拟地址上保存各自的私有数据。一个程序会故意写入内核页并触发故障；
另一个程序继续运行并正常退出。内核检查两者的结果，并要求独立 ABI 测试程序以
退出码 0 结束，随后回收全部三个进程，恢复到精确的空闲物理页基线，再报告
`NATIVE RUNTIME PASS`。预期出现的 `NATIVE USER FAULT` 诊断属于验收测试，
并非意外的内核崩溃。

默认配置是实验性的纯整数 ABI，具有四个进程槽、有界镜像大小，以及经过检查的
控制台写入、时钟 tick、yield 和 exit 操作。可选的
[旧版 FP 所有权配置](docs/native-fp.md)单独进行测试，默认仍关闭。
非 PAE 分页没有 NX；该证明不提供 TLS、动态链接、通用文件/线程和浏览器运行时。
目前也不宣称支持通过字节码应用存储安装原生 ELF。
详见[进程契约](docs/native-processes.md)、
[进程内存](docs/process-memory.md)和[ELF 验证](docs/elf32-loader.md)。

## 独立的 x86-64 基础

通过显式构建的 [x64 目标](arch/x86_64/README.md)，已验证 BIOS/GRUB
长模式入口、四级内核态 W^X 映射、NX/WP 和带 guard 的异常栈。
其[物理页池](arch/x86_64/FRAME_POOL.md)从经过验证的启动信息中选择真实可用内存，
排除仍在使用的分配、清零物理页，并测试精确的所有权、复用与回收。
初始页池最多管理 64 MiB 地址以下的 8 MiB 内存；这是有界服务证明，
并不涵盖机器上的全部可用内存。

```sh
make -f arch/x86_64/Makefile
python3 arch/x86_64/tests/boot_qemu.py --output /tmp/gtos-x64-boot-new
python3 arch/x86_64/tests/frame_qemu.py --output /tmp/gtos-x64-frames-new
```

单独启用的[稀疏 VM 核心](arch/x86_64/SPARSE_VM.md)现在可以保留大范围虚拟地址区间，
无需分配等比例的物理内存；它可提交清零页、设置 R/RW/NONE 保护，
并在检查所有权的前提下解除提交或释放，失败时进行回滚。
生命周期操作还包括确定性的 discard、具有所有权约束的 reset、精确 trim、
split 和 punch，并明确保证生命周期及相邻区域不受影响。
测试会保留约 1.35 TiB 虚拟空间，但只为选定页提供物理内存；
这是虚拟地址空间，并非物理 RAM。运行完整 guest 验收：

```sh
python3 arch/x86_64/tests/vm_qemu.py --output /tmp/gtos-x64-vm-new
```

它仍是仅由 BSP 使用的内核态服务，具有固定的元数据和提交上限，
不提供用户 ABI、桌面、线程、可执行/JIT 映射或浏览器。
[私有 x64 用户态门禁](docs/x64-user-runtime.md)是后续架构工作；
现有 i386 桌面及其测试套件继续维护。

## 验证

```sh
make test
python3 tests/desktop_qemu.py --output /tmp/gtos-modern-new
python3 tests/desktop_language_qemu.py --output /tmp/gtos-language-new
make GTOS-legacy.iso
python3 tests/qemu_smoke.py --iso GTOS-legacy.iso --output /tmp/gtos-legacy-new
```

确定性测试套件验证真实的 i386 分配器、调度器、VM 和存储代码。
如果主机无法执行 i386 Linux 二进制文件，可安装官方 `qemu-user`，
并将 `qemu-i386` 放到 PATH 中。QEMU 验收还使用 Pillow 检查截图和真实挡板运动。
每次运行都应使用新的输出目录。

验收涵盖 32/64/128 MiB 内存、一个或四个固件 CPU、启动分配器检查、
真实调度器的 sleep/yield/return、GUI 键盘输入、应用安装/启动/重启、
真实游戏挡板运动，以及在全新虚拟机启动之间安装、移除和重新安装应用。
调试日志和监视器会区分已检测 CPU、就绪/忙碌/失败的 AP 工作线程、
已完成任务和仅由 BSP 执行的通用调度。每个就绪 AP 会反复执行有界整数任务，
BSP 则验证结果和硬件 APIC 身份。安全 AP 启动已在一、二、四、八个 CPU 上测试，
还测试了缺失 AP 的超时和无 APIC 时的拒绝行为。
工作池另外验证了共享分页、缓存兼容性、每个工作线程的故障处理以及 BSP 定时器继续运行。
详见[工作线程契约](docs/cpu-work-pool.md)。

默认构建使用 -O2。需要未优化构建时，在 `make clean` 后运行
`make OPTIMIZATION=-O0`。`data/` 下的应用数据不会被构建清理删除。

BSP 现在启用了真实的非 PAE 分页：第零页不存在，
内核 text/rodata 在 CR0.WP 启用时为只读，设备映射均显式配置。
有界原生进程层在这一共享模板之上增加了独立用户地址空间；
i386 非 PAE 目标仍不具备 NX 保护。

中文字符图集覆盖每条本地化界面和候选词字符串；
任意 Unicode/CJK 字符覆盖以及通用输入法仍是未来工作。

更多内容：[语言与输入](docs/desktop-localization.md)、[设置](docs/settings.md)、
[桌面](docs/desktop.md)、[分页](docs/paging.md)、[内存](docs/memory-management.md)、
[CPU 与调度器](docs/cpu-management.md)。

[原生 Chromium 项目](docs/BROWSER_PORT.md)正在通过已验证的内核/API 前置条件和独立
Linux 参考构建逐步推进。Chromium 和网页渲染尚未在 GTOS 中运行。
当前路线按[真实集成主线](docs/chromium-integration-plan.md)推进：完整 V8 构建与链接、
GTOS Isolate/JS、本地 Content 网页及交互，再验收 HTTP/HTTPS 和音视频。
当前交付架构为 IA32，x64 保留为备选。真实静态 V8 ELF 已在 GTOS CPL3 完成
Isolate/JS 及退出回收，最新脚本范围见[工作负载补验](apps/native_v8_launch/workloads/README.md)。
大镜像装载时的堆/栈失败与后续修订保留在对应模块，浏览器仍待实际运行。

## 32 位 Chromium 原生运行库诊断

已验收的[IA32 运行库子集](apps/native_v8_runtime/README.md)包含 VM-backed heap、
new/delete、emutls、C++ TLS 析构、GTOS 单调时钟，以及 libc++ 单任务锁和线程身份接口。
来源锁与独立构建工具固定实际已执行对象，必须显式启用私有诊断 ABI。
完整 V8 Isolate 的当前资格见上方原生脚本模块；Chromium 网页、视频与 HTML5 尚未运行。

原生进程新增[RTC/PIT UTC 只读接口](docs/native-realtime.md)，独立于既有单调 ABI。
已验证日历格式、实际 CPL3 边界和资源回收；它尚未接入完整 Chromium/V8 浏览器。

已验收的 [V8 UTC 探针](apps/native_v8_utc_probe/README.md)把真实 CMOS/PIT UTC 接入 Time::Now、NowFromSystemTime 和 ToJsTime；独立验证正常退出、故障、取消及页面/FP 回收。它仍是完整原生浏览器的前置模块。

已验收的 [C 时钟/TLS errno 组件](apps/native_c_clock/README.md)提供真实 clock_gettime、固定 LLVM thread_local errno 和配套分配器，覆盖跨页写入、失败、取消及精确回收；完整原生浏览器仍待闭合。

已验收的 [C nanosleep 组件](apps/native_c_sleep/README.md)按真实 PIT 单调时间等待，接受完整有符号64位时长，验证零时长、跨页请求、非法参数、故障、取消及精确回收。导出对象与实际 GN 对象逐字一致；真实 Abseil SleepFor/Now 已在独立 CPL3 诊断中执行，完整原生浏览器仍待闭合。

已验收的 [C 字节/字符串组件](apps/native_c_string/README.md)提供13个默认LLVM原生接口，覆盖对齐、重叠、页尾终止符、非法读写、取消及精确回收；O2对象与实际GN对象逐字一致，完整原生浏览器仍待闭合。

已验收的[通用文件存储后端](docs/littlefs/README.md)保留完整 littlefs 读写算法，覆盖真实 ATA 持久化、文件和目录操作、所有者句柄回收、满盘及中断恢复；已验收的[原生文件 ABI](apps/native_files/README.md)接入真实 CPL3 用户页检查、文件和目录操作及延迟退出回收。

已验收的[原生 C 文件流组件](apps/native_c_file/README.md)提供固定 LLVM libc 的 21 个普通文件字节流接口，覆盖实际 ATA 读写、错误/EOF、缓冲失败、进程故障/取消及精确回收。标准流、完整 stdio/libc++ fstream、生产文件服务挂载、磁盘 PNG 到桌面窗口及完整 V8/浏览器仍待集成。

完整 V8 构建驱动的[真实 Abseil 退出验证](apps/native_v8_exit_probe/README.md)已在
GTOS CPL3 验证正常返回、立即退出、明确拒绝信号安装及精确回收；完整目标越过
日志消费者后，[实际熵消费者](apps/native_v8_seed_probe/README.md)通过 O0/O2 的
RDRAND 开关、边界、用户故障及回收验证。修复未用 POSIX 头条件后，完整目标
越过 futex waiter/mutex，当前首个阻塞为 zlib/zconf.h:505；实际 waiter 为标准
C++，条件等待原生提供者仍缺。完整 Isolate/JS 和网页运行仍待验收。
