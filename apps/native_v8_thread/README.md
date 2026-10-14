# GTOS 原生 V8 线程适配

固定 IA32 V8 的 `Thread` 现在通过 GTOS 原生 CREATE/TRY_JOIN 启动和等待
独立用户上下文。实际调用 `NotifyStartedAndDispatch`、子类 `Run`，在自己的
线程内执行 TLS 析构，Join 完成后回收守卫页与工作栈。

`Semaphore` 使用原子许可计数与调度 Yield，Signal release 与消费 acquire
配对；WaitFor 使用真实单调时间差。V8 LocalStorageKey 的登记按代际加锁，
各线程的值使用真实编译器 TLS，删除并复用 key 后旧代际值视为空。

验收采用真实固定 V8 Thread 子类。32 MiB/1 vCPU 与 64 MiB/4 vCPU 两配置
验证 8 轮工作线程创建和 48 次 Join；默认 64 KiB 栈、独立线程 ID、栈、
TLS/key/errno、许可消费和超时、线程槽不足及无效栈回滚、取消、TLS OOM、
页故障回收通过。两种错误共享存储负对照被拒绝。默认单线程模式额外验证
Start 失败且不创建 worker、不泄漏堆或页帧；合计 76 个上下文精确回收。

已有 FOpen 调用者加入候选平台对象后，stripped ELF 与此前验收版本字节
相同，因此沿用此前 8 任务/8 回收证据；本轮没有声称重新运行 FOpen 客机。
新 GN 图为 5179 个对象，原有 5177 条编译命令与最终输入顺序保持。
完整45按真实顺序重建并核对38个 libv8_libbase 归档成员，只替换平台成员，
旧缓存不变。Thread/Semaphore 缺失符号消除，当前首错为
`v8::base::OS::CreateTimezoneCache()`；后续诊断仍非穷尽，完整 ELF 尚未形成。

本模块仍限定四个原生上下文、BSP 调度和默认线程优先级。其他优先级明确
返回启动失败；其他尚未同步的库和 VM 服务保留现有限制。64 KiB 默认栈
已在探针中验收，整个 V8 调用链栈界仍未证明。

`acceptance.json` 记录私有验证的终态哈希与发布字节对应；`probe/` 是该次
验证的原始源码，需要固定 V8/GTOS 构建环境。本次不代表公开工具或远程 CI
已重新执行。完整 V8 Isolate、Chromium 网页浏览、视频播放和 HTML5 尚未
运行或验收，生产镜像本模块未改动。
