# 原生 IA32 C 目录与 Perfetto 线程身份

本模块把真实 `opendir/readdir/closedir/rewinddir/dirfd/dup/close` 接到 GTOS
目录和文件系统调用，并让 Perfetto ThreadChecker 使用真实原生 thread_id。
完整原始 TracingMuxer 已按实际 GN owner 编译；目录和 ThreadChecker 的实际对象
另与已有真实 C 运行库链接，在四组 GTOS CPL3/ATA 客体中通过运行验收。
完整 Content 和浏览器尚未运行。

## 接口与所有权

`overlay/` 包含九个文件，按 Chromium 源树相对路径组织。两个 BUILD.gn
保留原有成员并把 directory_native 加入真实 Content Shell 本地依赖。
目录接口只接受原生 IA32，公开 dirent 为一字节类型及 256 字节名字数组；
不声称与宿主 Linux 的 dirent 二进制布局兼容。

每个 DIR 拥有实际内核目录句柄及独立的 readdir 缓冲，使用真实 malloc/free。
同一 DIR 的操作需要调用者串行化，独立 DIR 没有共享用户态缓冲。
readdir 在 EOF 返回空指针并保留 errno；内核错误映射到当前 LLVM libc 的
errno 常量，返回信息的版本、类型和名字终止边界也会检查。

dirfd 返回借用句柄，dup 创建实际内核别名；别名共享底层文件偏移或目录游标，
最后引用关闭时回收。提前 close 借用句柄会使该 DIR 的后续操作失败。
closedir 在原生 close 报错时也释放 DIR，因为内核已经失效该句柄。
这些函数不提供宿主标准流文件描述符、fork、完整 POSIX 或任意悬空指针检测。
FILE/stdout 的接口仍遵守各自已经验收的原生契约。

ThreadChecker 的 GTOS 分支从真实 PROCESS_INFO 获取非零 thread_id，保留上游
atomic、detach、copy、assignment 和比较交换语义。其他平台分支保留。
sys/types.h 的补充仅公开已有 LLVM uid_t 类型声明，没有新增用户身份服务。

## 已验收

完整 Content 根 `//content/shell:content_shell` 已在最终九文件源视图重新生成，
七组原编译 owner/成员保持。1028 个完整 MD 逻辑别名全部保留：1001 个源文件、
10 个已有生成头引用、17 个 Clang 资源文件。生成头仍绑定已有合格缓存，
本轮没有全量重新生成，也没有完成整个 Content 构建闭包。

实际 directory、ThreadChecker 及完整原始 tracing_muxer_impl 三个对象通过
完整 M/MD/CXX 与输入终验。Muxer 编译保留原 GN 的 O2、DCHECK、EXTENSIVE
hardening、SSE2/emulated TLS 和静态插件配置；编译通过不代表 Muxer 已链接运行。

O0/O2 测试消费者链接实际 directory/ThreadChecker 对象及按原顺序保留的 133 个
已验收 C 运行库对象或整数归档。新消费者和旧运行库通过原始 C 接口连接，旧私有
STL 命名空间对象不跨接口。704 个输入及路径终验通过。C11 声明检查通过，
x86_64-none 和 i686-linux 均被两个原生头的明确 ABI 守卫拒绝。

矩阵为 `O0/O2 × 32MiB/smp1、64MiB/smp4`，每组包含独立 peer、三次正常进程
和一次整组页故障。smp4 仍按现有 BSP 调度契约执行。每组检查：

- 空/过长/不存在/非目录路径、独立缓冲、共享目录及 FILE 游标、EOF/errno。
- 已关闭、无效、外进程句柄及全局 64 槽满后恢复；292 次 DUP 中 272 次成功、20 次预期拒绝。
- 64KiB 实际堆耗尽时 opendir 返回 ENOMEM，期间内核 DIR_OPEN 次数为零；释放后恢复。
- 四次最终 close 的真实 Flush 失败返回 EIO，句柄仍释放，再次 close 为 EBADF。
- 每轮故意遗留五个真实别名；正常、回收 I/O 错误和整组故障都释放五个，peer 文件和游标继续工作。
- 三个 worker 的真实身份、detach 迁移、copy/assignment、join、槽复用、独立 errno 和 TLS。
- 正常进程四次 TLS 构造及析构；页故障进程三次构造、零次用户析构，仍完成整组内核回收。
- 16 个 FP 上下文的初始化/失效及 XMM/x87/MXCSR 保存恢复，异常计数为零。

| 配置 | peer 就绪基线 | 最终空闲帧 / 启动前基线 |
| --- | ---: | ---: |
| O0 / 32MiB / smp1 | 6776 | 6911 / 6911 |
| O0 / 64MiB / smp4 | 14960 | 15095 / 15095 |
| O2 / 32MiB / smp1 | 6788 | 6922 / 6922 |
| O2 / 64MiB / smp4 | 14972 | 15106 / 15106 |

每轮回收精确回到 peer 就绪基线；最后 peer 退出再回到启动前基线。
四组共 1168 次 DUP、1088 次成功、80 次预期拒绝、16 次最终关闭 I/O 错误，
84 个 owner 遗留句柄和 64 个 FP 上下文回收。46 个内核 C/C++ 对象使用完整
M/MD 检查，只在新专用磁盘中测试，并检查前后各八扇区哨兵。

## 证据与重放边界

[acceptance.json](acceptance.json)保存实际观测值和逐文件复制摘要；receipts
保存四阶段原始状态、隔离守卫和入口结果，logs 保存四份原始客体日志。
provenance 保存原始 Git 来源、真实 GN owner、完整依赖绑定和保留运行库清单。
probe 保存此次执行的用户程序、C 头检查、加载脚本和规范内核观察器。

replay 是已执行入口的逐字节快照。原入口固定在
`F:/GTOS-Chromium/artifacts/native-directory-20261017-a/`，依赖既有锁定源树、
生成头、Clang/QEMU、整数归档及已验收运行库；它们拒绝已有输出目录，不能
从仓库快照随意换路径执行，也不能宣称空白主机冷构建已经通过。

执行保持[本地命令栅栏](../../docs/native-command-execution.md)：固定工具摘要、
参数数组、最小环境、Python -I -B -S、独立无网络命名空间、no_new_privs、
Landlock 精确文件执行权限和新目录写入限制。旧失败、来源及缓存保留，构建
不触发 Git、QQ 或生产镜像更新。源码版权头保留，许可证见同目录文件。

历史失败和自审见 historical-rejections.json、self-review.md。下一步继续从
真实 Content 消费者的剩余依赖推进本地首帧和输入/DOM，再进入网络、视频及 HTML5。
此前原生 V8 Isolate/Context/JS 与工作负载验收仍有效，本轮没有重跑完整 V8。
