# 真实 IA32 V8 工作负载补验

在原有完整 V8 原生入口上增加对象/数组、ArrayBuffer/TypedArray/DataView、实际 GC、
Promise 微任务与预期异常恢复。所有脚本均在 GTOS CPL3 中的真实 V8 Isolate 执行。
仅重编 `main.cc`，继续使用锁定 GN 参数和原有对象；没有改动内核或运行时实现。

## 检查语义

- 保留 `GTOS42` 与 `3.75` 两项既有表达式检查。
- 64 个对象各含数组，经原型属性读取、reduce 与 join 检查得到 `64|GTOS|2016|7:8:9`。
- 一个 64 字节 ArrayBuffer 由 Uint8Array 和 DataView 共同访问；核对小端整数、
  Float64、subarray 别名及端点字节。
- 在同一 Context 的全局对象中保留 buffer，调用 LowMemoryNotification；必须实际
  收到 MarkSweepCompact GC prologue 回调，再核对 GC 后的长度、端点和浮点数据。
- 使用显式微任务策略。Promise 回调在 checkpoint 前保持值 0，执行真实 checkpoint
  后变成 42。仅 Promise 创建成功不足以通过。
- JavaScript 主动抛出 `Error('GTOS expected')`，要求 TryCatch 捕获且异常文本正确；
  随后同一 Context 再执行脚本并得到 133，证明异常后仍可继续消费。

掩码每一位只在对应结果检查通过后写入；GC 次数只由 V8 回调递增。内核根据 ELF
实际 RW 符号地址，通过受检查的 ReadMemory 读取两个值。掩码必须为 127、GC 次数
必须大于 0，且整个 V8 Dispose、平台退出和原生终结完成后仍为 stage4/result0/exit0、
无用户故障。停止进程的所有物理帧必须由 Reap 精确归还。

## 证据与限制

32MiB/smp4 与 64MiB/smp1 均通过：各 6159 次系统调用、掩码 127、实际 GC 回调 2 次，
stage4/result0/exit0、无故障。初始 2513 驻留页，退出时 2531 页，其中动态页 18；
独立大块为 0。Reap 后空闲帧分别精确恢复 4427 与 12611，不把退出前动态页记为 0。
完整62 ELF 为 12,933,488 字节、2385 个装载页；保留原启动配置和内核资源预算。

双配置原始客机日志、入口、内核观察器与机器证据在本目录。`acceptance.json` 绑定
真实编译、完整链接、客机终验、文件哈希和实际观测值。工作区详细记录位于
`artifacts/native-v8-workloads-20261015-a`；前三次未通过输入绑定的运行亦保留。
574 个实际 API 头文件已与固定 V8/libc++ Git blob 核对；GTOS v8config 覆盖头单独
绑定既有公开锁清单。编译前后检查完整 M/MD 依赖和输入/别名未变。
链接阶段复用已锁定的成功编译证明，并重新核对所有实际对象、归档、脚本和工具；
没有重新遍历前序完整链接证明中的 12073 项历史源码输入，不将其记作冷构建重放。

仍是单用户线程、jitless、关闭 Wasm/ICU/sandbox 的显式诊断配置。这些有限用例不能
代替完整 JS 测试套件、压力测试、冷构建、空白主机 bootstrap 或 Chromium 浏览器。
Content/DOM、网络、视频和 HTML5 均未在本模块验收，生产镜像未更新。
下一步建立真实 Content Shell 本地页面目标及 GTOS 平台依赖图，再推进首帧、输入、
DOM 更新和窗口关闭/回收，之后才进入受控网络和音视频。
