# 原生 V8 数值、字符串与模块化 printf 提供者

本模块补齐真实完整目标暴露的九项 libc 缺失：`ldexp`、`pow`、`trunc`、
`lrint`、`fabsf`、`strtoul`、`strdup`、`isalnum` 和 `__printf_modular`。
原生小程序已在 GTOS IA32 CPL3 运行；完整 V8 ELF、Isolate/JS、浏览器、视频和
HTML5 尚未验收。

固定 LLVM libc revision 为 `ebe33e01982dbbf879661e3b6b78450f3020a53f`。
数值与字符算法采用其真实函数体，保留 pow 准确计算路径。strdup 使用真实分配器，
返回独立、可 free 的副本。模块化 printf 入口沿用 generic printf 的 ArgList、
完整浮点转换器、原生 FILE 和整次格式化锁；关闭 stdout 时报告 EBADF。

## ldexp 的 GTOS 本地约定

GTOS 在所有四种舍入模式中统一报告不可表示的溢出和确定的非精确极小结果：
ERANGE 及相应 overflow/underflow、inexact 状态。入口以 64 位整数计算缩放后的
指数，避免 INT_MIN/MAX 运算溢出；结果值仍由固定 LLVM helper 选择。
零、Inf、NaN、正常指数和可精确表示的次正规结果保留既有路径。

非默认舍入下的 errno 要求存在[WG14 公开讨论](https://open-std.org/JTC1/SC22/WG14/issues/c2y/issue1100.html)。
这里记录 GTOS 的明确实现约定，不把所有上游行为描述为普遍的 ISO C 违规。

## 实际资格与边界

默认与 BSP 四上下文控制各运行 32M/单 CPU、64M/四 CPU 两次冷启动，合计
44 个程序、68 个上下文、24 次 Join、44 次 Reap。核对真实 CS/CR3、启动/析构
顺序和每次停止后的帧、文件句柄、FP 资源回收。四个 vCPU 不代表用户线程 SMP。

- 每个上下文覆盖四种硬件舍入模式、正负零、Inf/NaN、long32 边界、INT_MIN/MAX
  指数、正常范围错误及大指数下的精确缩放。公开 FE 码与 x87/MXCSR 位显式转换。
- 五组非整数 pow 参考值用大整数精确多项式区间和中点比较生成，涵盖四种舍入；
  生成过程不调用目标函数或宿主浮点数学库。这不是全输入正确舍入证明。
- isalnum 穷举 EOF 与 256 字节；strtoul 检查 2..36 基数、base0、无数字、非法
  基数、32位 ULONG_MAX、正负溢出和 endptr。成功路径保留原 errno。
- strdup 覆盖空串、嵌入 NUL、255 个非零字节、保护页前的字符串、真实 OOM、
  free 后恢复和独立副本。原堆容量未扩大。
- printf 验证 2067 字节真实文件内容，含 NUL、2049 字节字符串及浮点格式；
  检查只读/关闭 stdout。三工作线程保留 C++ 输出，再输出独立 PRNT 标签，
  由内核分别核对三条 C++ 记录、三条 printf 记录以及原文件写入。
- 原 FILE、临时文件、真实磁盘错误、64 句柄边界、同步、TLS/C/global 析构、
  raw exit、abort、UD2 和外部取消回归保留。20 个非法复制程序仍精确停止且
  目标保持原字节；内核代码仍通过通用寄存器 ISA 审计。

GN 真实图保留原 5187 个对象命令，仅增加九项，成为 5196 项。
`action_pool_depth=1` 仅固定宿主构建并发数，避免额外 CPU 数量解释器查询；
原有目标编译参数和旧链接输入相对顺序保持。

完整57实际 LLD 返回1，本组九项缺失消失，诊断完整返回七项剩余缺失；首项为
`v8::base::debug::EnableInProcessStackDumping()`，其余为栈回溯与共享内存相关接口。
旧1101个最终输入保持相对顺序，新增九项后共1110项；原38成员归档不重建。
完整结果由 `acceptance.json` 和集成主线记录。生产镜像没有变化。
公开源文件与所测私有副本逐字节核对，不声称重新执行了公开 canonical 构建或
完成空白主机 bootstrap；全部旧缓存和失败产物保留。
