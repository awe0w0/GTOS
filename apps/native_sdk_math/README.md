# 原生 SDK 的 log、exp、round

真实 V8/Content 扩展根目标缺少 C `log`、`exp`、`round`。本模块通过
`base` 的 GTOS 条件依赖接入独立 SDK source_set，使用固定 LLVM 实现及其
完整精度回退、原生 SSE2/fenv 和逐线程 errno。没有禁用 BoringSSL 线程或
改变其他平台的数学配置。

上游 `exp` 在向下、向零舍入时直接饱和到最大有限数，提前绕过溢出异常。
真实 CPL3 测试复现该问题。本补丁只在 GTOS IA32 的原有正有限溢出区间
补报 `FE_OVERFLOW`，保留数值结果、errno 和当前舍入模式；NaN、无穷、
负数及其他平台沿用原路径。来源与源码摘要见 `source-lock.json`；LLVM
代码使用 [Apache-2.0 WITH LLVM-exception](../native_v8_integration/LLVM-LICENSE.txt)。

## 接入范围

`overlay/` 保存两个实际编译的覆盖文件。`base-native-math.patch` 仅添加
`base/BUILD.gn` 中已初始化 deps 后的 GTOS 依赖；没有包含本机其他尚未
验收的 base 适配。新增库采用 `LIBC_ERRNO_MODE=4`，继续调用已有外部线程
后端，不另建全局 errno。原始 log.cpp、round.cpp 来自锁定 LLVM 提交。

这些文件供后续真实 Content 源视图集成。现有完整 V8 工作负载的冻结覆盖层
和锁文件保持原版本；本模块没有修改旧通用构建入口，也没有宣称空白主机
冷构建或完整 Content 已通过。数学对象已在实际 Content GN 配置中编译，
并加入保留真实入口、main 和原有顺序的完整扩展根链接。

## 验收

最终客体及输入终验通过，结果见 `acceptance.json`。独立 600/900 位 Decimal
及精确有理数编码生成 165 项输入：
log 45、exp 72、round 48，覆盖四种舍入模式、正负零、阈值邻值、次正规数、
无穷、quiet/signaling NaN、errno、异常位和调度前后的 FP 状态。

`probe/` 是实际执行源码；cdecl 汇编桥以整数搬运输入位，避免调用者提前
quiet signaling NaN。保留 exp2 的默认全部 2098 个整数幂及线程边界检查，
另检查缓存中的 ceil、floor、fmod、pow。正常八轮线程、取消、实际 TLS OOM、
页故障和共享 errno/TLS 反例全部通过。两种内存/CPU 配置共 76 个实际 CPL3
上下文、48 次正常 Join；100320 次新增数学调用、52048 次 exp2 调用和
1216 次缓存数学回归共 153584 次。物理页、VM 与 FP 所有者恢复基线。

32MiB 的默认及正常八轮阶段在旧聚合宿主超时前已经完成，复核全部输入、
别名、日志及重编内核字节一致后才复用；取消/OOM/故障在独立启动中补验。
64MiB 完整组重新执行，两个错误存储对照按预期失败。旧失败聚合未被接受。
配置四 CPU 仍仅在 BSP 调度用户线程，不声明用户 SMP 或完整调用链栈界。

`fixture-abi/` 保存与复用小型回归内核匹配的四份历史头；它们不会覆盖当前
系统头，也不证明新版大映像装载 ABI。`probe/remaining-kernel.cpp` 只选择
未完成的异常用例，全部数学和回收断言与完整观察器相同。生产镜像未更新。

完整扩展根的未定义符号已从 17 降到 14，剩余 MD5 与 PMA；该根仍没有 ELF。
已验收的[原生 V8 JS 基线](../native_v8_launch/README.md)继续成立。新的
`::base` 扩展根、Content 首帧/DOM、网页浏览、视频及 HTML5 尚未验收。
后续按[路线 Issue #9](https://github.com/awe0w0/GTOS/issues/9)继续处理真实阻塞。

执行继续遵守[本地命令栅栏](../../docs/native-command-execution.md)：固定且
已审阅的脚本和工具、最小环境、Python -I -B -S、无网络命名空间、
no_new_privs、Landlock 精确执行白名单及新输出写权限。历史失败、镜像和
缓存全部保留。公开源码复制需逐字节核对；未另跑公开构建器或远端 CI。
