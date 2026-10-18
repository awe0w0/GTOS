# 本轮执行前复审

2026-10-18，基线 dev/origin/dev `c1e3ff623d41a9f8abc1c0fb3935f4bcf1ed1c07`。
既有推送已结束，本轮没有再次请求登录或读取凭据。源码、旧缓存和生产镜像保持原位。

执行栅栏继续使用固定工具 SHA、静态参数数组、Python `-I -B -S`、最小环境、
独立 user/net 命名空间、no_new_privs 和 Landlock ABI>=6。只有各阶段的新输出可写，
输出不能执行；工具目录没有整体执行权限。Git/QQ 不由构建入口调用。
QQ 02:00–09:00 静默；没有排队补发。隔离失败、未知依赖、后置检查失败均返回非零。

执行前发现并修正：

- GN 新增两个 source_set 和一个 group 会增加目标清单。逐行保留旧规则和动作，
  只准入两个明确 subninja 和精确 group/harddeps 元数据，终验再次核对全部已审核 owner/toolchain。
- 小写 `.s` 的真实 Clang 规则不生成 MD。保留原 argv、要求无 depfile，按唯一锁定源码、
  无 include/incbin、精确 ignored 参数日志和对象资格验证；三个 C++ 单元仍全部执行完整 M/MD。
- Popen 后状态写入也纳入清理范围；launcher 对本次新进程组使用 subreaper 回收后代。
  不枚举其他进程。清理异常或遗留后代不能返回成功。
- 新观察内核的 M 和 MD 都纳入终验；用户侧 ISA 对象逐一与链接输入哈希交叉核对。
- 链接器可正常 GC 未被消费的类成员。完整原格式对象用于来源和全部代码指令检查，
  最终 ELF 核验实际消费的精确 ABI，不能强行保留无调用成员来制造完整运行结论。

负例预期来自原始实现：非 ASCII CustomPlatformType 触发原 CHECK，真实 abort 转发
heap panic(0x21)，以 SYS_EXIT `0x48000021` 终止。验收还必须看到 stage10、无 fault、
未经过常规 exit 标记、精确 Reap/帧/任务/FP 回收。普通非零、fault 或调用返回均不能算通过。

这些是执行前逻辑与资源边界检查，不构成尚未执行的原生语义、Content 或浏览器验收。
