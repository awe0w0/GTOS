# 原生 nearbyint

完整41的真实首错是`nearbyint`，V8在`CheckAtomicsPauseIterationNumber`
使用它判断`Atomics.pause`参数是否为有限整数。本模块向既有numeric库
增加固定LLVM实现，保留IA32/SSE2、C++20、errno和引擎参数；旧5173条
命令及最终次序保持。没有用返回输入值等替代实现消除缺失符号。

GTOS/i386分支直接读取MXCSR舍入位，然后调用固定LLVM的整数舍入算法。
该适配避免generic模式探测的浮点算术潜在影响异常标志；旧公共FPUtil头
和数学/解析对象保持。原来源未修改，其他架构路径沿用原实现。

真实C符号由volatile、SDK中明确的double/noexcept函数指针调用，禁用
消费者builtin替代。32组独立IEEE64向量覆盖正负零、半值和相邻数、
非正规数、最大有限数、无穷、quiet NaN及大整数，四种舍入模式与两个
直接核验的inexact初态同时检查数值、errno、控制字和标志保持。

初态采用既有FP测试的FNSTENV/FLDENV和MXCSR种子。早期算术准备方法
在调用前失败并保留，不能算作库失败或通过。环境种子版本的原提供者
对照已通过，因此没有宣称该客体已经观察到generic新inexact副作用。

五种行为检验正常返回、malloc失败后释放重试、保留输入的循环取消、
带活跃堆的故障，以及跨定时器/系统调用的两种标志初态。第六个任务
在正确计算后有意增加MXCSR标志，消费者必须真实exit91且精确回收。
两配置合计12任务包含这两个负控任务；结果见[验收记录](acceptance.json)。

消费者编译绑定固定GN图/参数、工具及实际完整-M/-MD闭包；GN和完整
目标重放另外检查完整图和输入。公开字节对应不等于公开GN/客体、
远程CI或新stock冷读回。单函数/入口栈观测不证明完整调用链上界，
多CPU配置不证明并行用户线程。完整V8 ELF、Isolate/JS、浏览器、视频
和HTML5仍待验收，生产镜像未改。
