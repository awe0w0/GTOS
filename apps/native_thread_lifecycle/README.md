# 有界原生线程生命周期

完整44的实际首错是 V8 Thread 构造。现有进程槽位独占 Task、地址空间和 FP，
因此先实现真实共享地址空间任务的创建、退出与回收，再接入 V8。

`NativeRuntime::Activate(..., policy, true)` 显式启用本模块；默认仍关闭线程。
最多4个原生上下文合计共享既有4组独立内核栈与FP记录，在BSP上真实轮转，
有各自CPL3帧和用户栈，不宣称AP/SMP用户线程或每个进程4个worker。

新增ABI提供 CREATE、THREAD_EXIT 和 TRY_JOIN。worker入口必须位于已接纳
镜像的可执行段；caller提供独立VM reservation，首尾guard不可访问，中间
至少2页可写栈。创建在IF=0内先完整验证，再发布ID、cdecl参数和独立帧。
活worker和等待join的退出worker都保留整个栈reservation，禁止release、
trim、decommit、discard或保护修改，成功join后才能释放。TRY_JOIN对活
worker返回BUSY，调用者Yield并重试；完成时复制退出码并一次性消费记录。
这是尝试join ABI，不把它冒充内核阻塞等待或完整POSIX线程。

线程身份独立，FILE、VM和surface始终按进程所有者处理。worker退出不回收
父进程的文件或窗口；进程退出、外部取消或任一worker故障结束整组，boot
上下文先移除全部调度引用、退休每个FP记录，再销毁一次共享地址空间。
只有父进程资源按owner回收，避免worker退出释放仍在使用的CR3。

真实32M/smp1和64M/smp4各覆盖4组：11次创建/join及栈重复使用，带活
文件的整组取消，实际worker页故障，以及两进程/外进程worker的join拒绝。
每配置22个上下文（含外进程），总44个上下文精确回收、44次FP初始化/退休；
两配置可用帧各恢复原基线。主探针计724项检查；外进程运行中的计数未并入。
验证入口/栈/输出边界、创建容量、活/退出栈保留、自join/主任务join/重复join、
共享内存交错、独立MXCSR，以及真实worker访问父进程FILE、父进程正常关闭。
内核整数指令审查通过。首轮const编译错误和第二轮测试辅助函数警告保留，
未降低-Werror或删除失败日志。没有新硬件NX或完整调用链栈界结论。

候选内核默认线程关闭时，原真实V8 FOpen消费ELF另行通过8任务/8精确回收、
1036项消费者检查和314项日志负控。两种检查都锁定全部编译依赖与输入，
旧缓存只读，新磁盘和输出隔离。仓库文件与已测试私有源逐字节对应；这是
私有验证和公开对应，不宣称新公开工具重跑、stock冷读回或远程CI。

TLS、errno、锁和libc++abi guard仍按现有单用户任务约束运行；V8诊断入口
仍使用默认关闭线程策略。V8 Thread/Start/Join/Semaphore尚未接入，未重放
完整45。完整V8 ELF/Isolate/JS、Chromium网页、视频和HTML5均尚未验收。
生产镜像未改。后续先实现真实每线程TLS/errno和同步语义，再接入固定V8。
