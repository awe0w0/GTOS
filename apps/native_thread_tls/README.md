# 原生每线程 TLS、errno 与析构回收

IA32 原生 BSP 共享地址空间中的 TLS 生命周期模块，采用真实编译器
`-femulated-tls` 控制块和固定 LLVM libc external errno 实现。最多同时四个
原生上下文，由已验收的线程 ABI 创建、退出、join；不是完整 V8 线程桥接。

`thread-context` 通过真实自查询维护四个固定 owner 槽，errno 首次访问不分配
内存或访问 emutls。共享控制块只保存变量索引，每线程保存自己的地址表与
对象。保留 IA32 16 字节控制块、模板初始化、过对齐和增长溢出检查。
正常退出在调用线程执行 LIFO 析构，允许析构期间再注册；随后释放 TLS、
地址表及 owner 槽，再调用真实 THREAD_EXIT。主任务使用已有整组 EXIT。

验收使用 `probe/main.cc` 的真实 `thread_local`，没有替换正常消费者符号：

- 32M/smp1、64M/smp4 共 74 个上下文精确回收，75816 项消费者检查。
- 每组 8 轮创建/退出/join，25 个上下文；32 个 TLS 变量触发地址表增长，
  验证 256 字节对齐、非零模板与零初始化、并发存储/errno 隔离、槽位复用。
- 实际 C++ 构造/析构与析构中再注册，顺序为 2、3、4、1；析构访问自己的
  TLS 和 errno，并跨 Yield 保留状态。最终 heap query 为空。
- 整组取消、真实 TLS OOM abort `0x48000021`、worker 页故障后，所有帧、
  FP 和 task 引用回到基线。异常终止不伪造 C++ 析构回调。
- `shared-errno-control.cc`、`shared-tls-control.cc` 仅为错误存储负例，分别
  被消费者初始化检查拒绝；不进入正常 runtime 或完整目标。
- 新 GN 图保留 5173 条旧对象命令及最终输入顺序，新增 context 对象，
  仅 native_c_clock 三个对象从 errno mode3 改为 mode4；六对象实际复编，
  完整 pre-M/post-MD 依赖锁定，四个初步对象与正式图产物字节一致。
- 新提供者接入旧真实 FOpen/native-entry 后，默认关闭线程的 8 任务回归、
  8 次精确回收和 314 个日志负例另行验收。

正常 worker 必须使用 `gtos_native_exit_with_tls`。直接调用内核 THREAD_EXIT
不会替用户运行 C++ 析构。析构登记 malloc 失败仍按固定 libc++abi fallback
约定返回 -1；不承诺 OOM 下已构造对象的自动登记成功。没有 DSO 卸载、
async-signal、独立杀死持锁线程、SMP、NX 或完整调用链栈上界资格。
FILE、mutex、condition_variable、call_once、guard 的单任务限制仍保持，
不得据本模块直接启用多线程 V8。

参考为固定 llvm-project `62397f8b3c3986f54187ce08f00b3448ea1f8880`
的 compiler-rt emutls ABI，以及固定 LLVM libc 的 libc_errno.cpp。上游 errno
源码不改，只在 GN 和本地非分配 cell 接入 external4。

执行采用固定工具、最小环境、无网络命名空间、no_new_privs、Landlock
只写新输出和单文件执行许可。旧失败与缓存保留；公开源码与验收私有源码
逐字节对应，未重新运行公开 GN/客体或远程 CI，生产镜像未改。
完整目标仍以 full44 的真实 V8 Thread 首错为准，full45 尚未重放，
原生 Isolate、JavaScript、浏览器网页、视频和 HTML5 均未验收。
