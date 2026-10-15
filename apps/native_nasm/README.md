# GTOS IA32 NASM 格式适配

真实 Content 图在 `libaom_nasm` 处因 `_nasm_flags` 未定义而停止。本模块为
`is_gtos && current_cpu == "x86"` 增加 `-felf32` 分支，不借用 Linux/POSIX 身份。
同一 Content Shell 图加入此候选后已越过该错误，下一处是 `ui/menus/BUILD.gn:6`
的平台断言。GN 仍返回 1，完整 Content 构建及浏览器运行尚未通过。

## 补丁与来源

`gtos-nasm.patch` 应用于 Chromium 中的 **NASM 子仓库根目录**，不是 Chromium 主仓库：
NASM revision 为 `525a09a813be0f75b646ee93fc2a31c27b87d722`，Chromium 为
`04bca2584cddb39e134fc7c16bb2886e02863d0e`，libaom 为
`ae410fe8b7bd45f3cf61dc8f112dc783e2a08089`。子仓库 HEAD、主仓库 Gitlink、DEPS
以及所用源码的 Git blob 均已核对；摘要见 `inputs.json`。

补丁已在新目录实际应用，结果与资格候选逐字节相同，见 `patch-replay.json`。
Windows 默认换行转换曾使首次重放产生 CRLF；第二次仅为该 Git 调用指定
`-c core.autocrlf=false` 后得到相同 LF 字节。没有更改全局 Git 配置，首次目录保留。
本次只随 GTOS 仓库发布补丁，未修改 Chromium 或 NASM 远程仓库。

## 实际验证

复用已锁定的 Linux 参考构建缓存 NASM 3.01；先解析 ELF64、解释器及动态依赖，
确认宿主依赖为 `libpthread.so.0` 和 `libc.so.6`。这不是本轮从源码重建 NASM 的证据，
也不表示 GTOS 已实现这两个宿主库。编译和链接只在无网络、最小环境、no_new_privs、
Landlock 精确可执行文件白名单内运行，输出仅写入新的资格目录。

| 实际产物 | 字节数 | 检查结果 |
| --- | ---: | --- |
| caller.o / provider.o | 704 / 672 | ELF32/i386，精确导入导出、R_386_32 数据地址与 R_386_PC32 调用重定位 |
| bilinear.o | 1792 | 未修改的 libaom 双线性 SSSE3 汇编，6 个隐藏函数，无未定义符号 |
| eighttap.o | 4176 | 未修改的 libaom 八抽头 SSSE3 汇编，6 个隐藏函数，无未定义符号 |
| caller-provider.elf | 1028 | LLD 静态链接，无未定义符号或残留重定位，无动态解释器，入口指向调用者 |

所有对象均检查节、符号及重定位边界、不可执行栈、无可写可执行节和无 TLS。
四份 NASM depfile 分别包含 2、1、3、3 个依赖，全部属于事先锁定输入。
缺失 include 的真实汇编返回非零且无对象；另用 `-felf64` 生成真实错误格式对象，
ELF32 检查器明确拒绝。全部 23 项输入和执行工具摘要在结束时再次核对。
命令、产物摘要及正反例见 `qualification.json`，自审见 `self-review.md`。

这里使用与模板一致的格式、reproducible、include、define、depfile 和输出选项顺序，
通过固定参数数组直接执行工具。**这不是完整 GN/Ninja NASM 动作重放**：Content 图
尚未生成完毕。`qualify-v2.py` 与 `launch-qualify-v2.py` 保存实际运行脚本，固定引用
`/mnt/f/GTOS-Chromium/artifacts/native-nasm-20261015-a`，拒绝覆盖已有输出。
复现需要保留的 pinned 工作区、参考 NASM 缓存、inputs.json 中的原始路径与图记录；
本模块未提供下载、空白主机 bootstrap 或覆盖历史输出的入口。

## 下一步与边界

汇编配置只有 IA32/PIC 宏，未复制 Linux 的完整 libaom C、pthread、OS 或 SIMD
运行配置。没有执行这些对象，也没有检查滤波数值正确性、GTOS 客机 SIMD 运行及
任务切换；这些仍需在真实消费者接入时验收。两份对象不代表完整 libaom 编解码器。

继续检查 `ui/menus` 的通用代码依赖，再推进真实 libaom GTOS 配置、Rust std 后端及
Content 本地首帧与 DOM。浏览器、视频、HTML5 和动态加载仍未通过；生产镜像未变。
既有原生 V8 客机基线保留。执行约束见[本地安全栅栏](../../docs/native-command-execution.md)。
