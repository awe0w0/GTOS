# 原生 OS 错误与临时文件

本模块为固定 V8 补齐 `OS::GetLastError`、`OS::LogFileOpenMode` 和 `OS::OpenTemporaryFile`，并提供真实 LLVM libc `tmpfile`。这是 32 位 Chromium 的依赖模块；完整 V8、Isolate、JavaScript、浏览器、视频和 HTML5 仍未运行。

`GetLastError` 读取已有的每线程 errno，成功调用保留原值。日志文件使用固定上游的 `w+` 模式。临时文件先分配 FILE 和缓冲，再通过原生文件服务排他创建并在同一次 IF-clear BSP 调用中移除目录项；返回的句柄仍由 LittleFS 跟踪，支持正常读写、定位、同步和关闭。正常退出由真实 FILE 列表关闭，故障和取消由内核 Reap 回收。

文件 ABI 只增加 `TEMPORARY=0x1000` 标志，结构、版本和已有操作值保持。该标志必须与 `READ_WRITE|CREATE|EXCLUSIVE` 精确组合，不接受截断、追加或未知位。名称包含进程 ID 和原子序号；碰撞重试有界，序号不回绕。创建或删除遇到实际 I/O 错误时可能留下部分磁盘效果，错误不会被隐藏。

默认及四上下文两组控制均使用 32M/smp1、64M/smp4，覆盖 24 个实际 CPL3 程序、48 个上下文、24 次 Join 和 24 次进程 Reap。四上下文仍由 BSP 调度，不据此声明用户态 SMP 已支持。

验证包含独立临时文件的二进制读写、4097 字节数据、碰撞哨兵保留、真实 ENOMEM/EMFILE 及恢复、只读卷 EROFS、8 组非法标志和真实磁盘 I/O 错误。固定 64 KiB 堆在测试中先于 64 个 FILE 耗尽，因此将 FILE 分配边界和内核 64 句柄边界分别验证。所有成功临时打开都由内核独立检查：返回前路径已不存在，句柄具有真实 owner、普通文件类型及零初始长度。

生命周期覆盖正常返回、C exit、关闭 stdout 后直接退出、abort、实际 UD2 异常和安全内核上下文发起的取消。每次 Reap 后句柄为零、物理页回到基线，FP owner 精确失效；故障和取消无需用户态 fclose。

验收哈希和实际记录见 [acceptance.json](acceptance.json)，实现审查见 [self-review.md](self-review.md)。私有测试输入保留完整 M/MD、内核指令审计、无网络命名空间和精确 Landlock 执行白名单。生产镜像未更新；私有客机验证不表示生产虚拟机已更新。
