# 自审

- 根因有两层：GTOS 工具链缺少 rust_bin；真实链接又缺 compiler_builtins crate。保留 BoringSSL 测试定义及 Rust 标准依赖，没有关闭 Rust、伪造 Linux 或提供空内建库。
- 根据目标 JSON 使用直接 GNU LLD，未照搬宿主 Linux clang 链接 ABI。响应文件、extern/native 依赖、ldflags、环境、depfile 与 GN 生成值逐项核对，再用固定参数数组执行。
- 新工具限定原有 GTOS IA32 Rust 分支。八种旧工具规则与已验收 Ninja 定义完全相同；没有改内核、V8、C++ 运行库或生产镜像，因此没有重复无关客机矩阵。
- compiler_builtins 的静态配置来自未修改的包内 build.rs/configure.rs、上游 GN 特性和真实 target cfg。已审阅外部 clangrt 选项仅控制 sanitizer，没有用它掩盖缺失 crate。保留完整数学源码和目标可靠浮点特性；构建脚本执行、实际 GN crate 构建和完整运行时仍未验收。
- 归档检查区分代码、数据和空单元，所有对象都检查 ELF32/i386/ET_REL 与节边界。唯一空 CGU 检查精确形状及本地文件符号，没有把它当成代码，原失败保留。
- __udivdi3 的本地隐藏绑定是实际链接器输出，已核对定义、类型、可见性和入口分支目标。保留外部 C 函数/数据的全局 ABI 检查，没有降低未定义符号拒绝条件。
- 最终资格入口完整运行并终验所有输入、别名和工具；2076 字节 ELF 与前次缓存完全相同。缺失提供者返回非零、三个未定义符号、无 ELF。没有执行目标，未声称运行时数值、ABI、退出或资源回收通过。
- Content 图已实际越过 rust_bin 首错，仍返回 1，最新是 Linux 次级工具链的 CUPS 探测。四个 menus/libxml 文件按候选发布；已实测 pthread.h/fcntl.h 缺口，未把两份 C 对象计为完整 libxml。
- 所有脚本执行使用无网络、精确工具白名单和新输出限制，不从日志执行命令，不读取凭据或调整 hooks/全局安全设置。补丁在独立新目录应用，并验证原始文件、索引和提交树一致后再正常提交。
