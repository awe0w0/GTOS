# 原生文件句柄复制

`GTOS_SYS_FILE_DUP`（`0x4740`）使用现有的八字节
`GtosFileControlRequest {version, handle}`。成功返回当前进程所有的新正句柄；
失败返回文件 ABI 的负错误码。它不接受其他进程的句柄。worker 通过既有
`thread->process` 路由共享进程的文件所有权，退出回收仍以整个进程为单位。

FileStore 将 64 个句柄槽与 64 个稳定地址的打开资源分开。副本共享真实
littlefs 文件、访问模式、文件偏移或目录游标；独立 open 仍有独立偏移。
不复制 littlefs 链表中的 file/dir/cache，也不分配内核堆。
每次 close 立即失效对应句柄，最后一个引用才关闭底层资源。最后 close
即使返回磁盘错误也失效并释放槽；这不保证失败的写入已经持久化。
临时文件仍在创建时 unlink，最后一个别名关闭后释放资源。

句柄数仍是全局 64，包括不同进程和所有别名；满槽返回 `-24`，无效、已关闭
或外进程句柄返回 `-9`，单调句柄编号耗尽返回 `-75`。失败不发布新句柄，
也不改变现有偏移。DUP 只操作固定元数据，不读写磁盘、不分配物理页。
只读打开资源的刷新按资源遍历，保留既有独立 reader 的偏移和一致性处理。
文件服务继续由 BSP 的 IF 保护串行运行，不能在 AP 并发调用。

## 验证

`tests/native_dup` 是独立 ELF32 用户程序与真实内核/ATA 客体，不依赖私有
环境变量、CPU 时钟或 LLVM 适配。每个客体运行一个持续 peer、七次正常
进程及一次整组页故障退出，检查：

- 671 次真实 CPL3 DUP：567 次成功、104 次拒绝，全局峰值 64。
- 请求长度、版本、空/溢出/guard/跨页指针，以及只读、非对齐请求。
- 外进程、已关闭和无效句柄；三个 worker 顺序读取同一偏移并执行 join/槽复用。
- 目录与匿名临时文件的别名；关闭原句柄后副本仍可用。
- 八次 owner 回收后 peer 的文件和进程继续工作；最后回到零句柄。
- 注入一次磁盘 Flush 失败，回收准确报告 `-5`，继续释放其余句柄。
- 每次回收后的物理页基线，以及 32 个 FP 上下文的 XMM/x87/MXCSR 保存恢复。
- DUP 入口的 IF、调用者、其他通用寄存器、物理页和磁盘计数保持不变。

矩阵为 `O0/O2 × 32MiB/smp1、64MiB/smp4`。smp4 只验证 BSP 调度，
不代表 AP 用户线程支持。测试只格式化本次新建镜像的中间 2048 扇区，
检查前后各八扇区哨兵；不访问生产镜像或主机磁盘。
四组重放均通过，48 个 C/C++ 对象的完整依赖检查和输入终验通过；
共 2268 次 DUP 成功、416 次预期拒绝、128 个 FP 上下文回收。
[机器记录](../tests/native_dup/verification.json)及同目录 evidence 中的原始
日志保留精确计数、错误注入、物理页基线与工具/输入摘要。

## 离线重放

保留仓库位于 `<workspace>/sources/GTOS` 的布局，使用已锁定的本地 Clang、
QEMU 8 及 scalar integer 归档。所需 SHA256 在 `tools/native_dup/inputs.json`
和入口中固定；不下载、不安装、不加载环境脚本，不声称空白机器可冷构建。
在 WSL Ubuntu-20.04 中，最小环境入口为：

```sh
/usr/bin/env -i PATH=/usr/bin:/bin LANG=C.UTF-8 TZ=UTC /usr/bin/unshare --user --map-root-user --net /usr/bin/python3 -I -B -S /mnt/f/GTOS-Chromium/sources/GTOS/tools/native_dup/run.py
```

入口不接受覆盖参数。它要求 Landlock ABI≥6、no_new_privs、独立无网络
命名空间及精确可执行文件白名单；只允许写本次新建的
`<workspace>/artifacts/native-dup-<UTC>-<pid>`，禁止执行生成的宿主文件。
隔离或输入校验失败即停止。每个对象在编译前执行完整 `-M` 并核对源清单，
编译后比对 `-MD`，最后复核所有输入字节及路径别名。
`status.json`、`guard.json`、`launcher-result.json` 和原始客体日志一起保留。
不能只凭编译退出码或 QEMU 的结束码判断通过。

该模块为后续 C `dup/close` 和目录适配提供内核所有权基础；这些 C 包装、
完整 TracingMuxer/Content 与浏览器首帧尚未验收。本次不更新桌面启动镜像。
