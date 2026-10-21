# 原生 Content：Ninja 调度与增量试点

031 在新的试验目录中，用 Ninja 1.12.1 装载冻结 GN 边集，构建 integrity_policy 的
完整绑定头与 features 头。真实构建 rc0；同命令第二次运行输出 no work to do，
两个目标的字节、摘要和 mtime 不变。这证明了该小型生成子图的调度与增量行为，
不证明完整 Content、CXX、链接或浏览器可用。

## 1. 图与启动方式

原图来自 native-content-frontier-20261018-a/gn-clipboard-01/build。
root toolchain.ninja 为 45456305 B，SHA256
a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175。
工具 Ninja 为 sources/chromium-full/src/third_party/ninja/ninja，416480 B，SHA256
09f0e5a8a2cf762b24b4d3ed464ffb2529e650d2efc36bab31da36aa93791efc。

原图的 ACTION 前缀是只允许三种 GN 元数据脚本的 content-helper-clipboard-v1.py，
不适合作为 Mojo 运行时分派器。031 只在新目录中把该前缀替换成
/usr/bin/python3 -B -S，共 6476 处；四份 toolchain 文件与原文件的差异均仅为这项替换。
被调用脚本及其参数、依赖和输出列表未改。首次误加 -I 导致同级 generators 包无法导入，
真实失败保留；去掉 -I 后构建成功。不能把这个适配错误说成 Ninja 的缺陷。

试点入口 route.ninja 装载四个子图并保留 pool，未带原 build.ninja 的 GN 再生成边。
因此这是经过明确启动适配的冻结边集试点，不是未经修改的完整 GN/Ninja 生命周期验证。
原图子 manifest 分布在多个目录；031 复制了整份清单集合，但不能据此断言任何局部试验
都必须重复复制全部文件。

## 2. 真实生成与增量

目标为：

- gen/services/network/public/mojom/integrity_policy.mojom.h
- gen/services/network/public/mojom/integrity_policy.mojom-features.h

Ninja 自行调度五条 ACTION：模板预编译、parser、type_mappings、shared generator、
normal generator。成功构建 rc0，54.013 秒。主要目标：

| 目标 | 字节 | SHA256 |
|---|---:|---|
| integrity_policy.mojom.h | 9469 | 3e4ab066f4a1d3581a4c3fc0516f857bbf4db06d63f784f7632e4840ed6500e2 |
| integrity_policy.mojom-features.h | 498 | f00ae20cb1d60e90012e621b243cb01fc4cdfc5348b85b6717fd85ce0d410608 |

模块相关产物 14 件：normal 5、shared 7、module 1、map 1；另生成模板归档一件，
.ninja_log 共记录 15 个输出，不能把总物理产出说成只有 14 件。
与 030 的五件 normal 产物逐字节相同；module 也等于当时的暂存输入，
但它不是 030 那轮新生成的第六件产物。

第二次同命令 rc0、39.108 秒、零条命令，输出 no work to do。已记录的两个目标
字节、SHA256、mtime 均不变。前者证明目标可由 Ninja 构建，后者证明同一试验配置下的增量复用。

## 3. 配置产物不需要新增契约

手工复制的构建目录最初缺少 mojom_integrity_policy.build_metadata，Ninja 正确报
missing and no known rule to make it。031 复制了十个同模块 GN 元数据文件作为种子；
复制数量不等于本试点实际消费了全部十个文件。

源码已经明确规定这些文件如何生成：mojo/public/tools/bindings/mojom.gni:684–703
通过 generated_file 写 build_metadata，:789–791 等位置通过 write_file 写 rsp；
build/rust/std/BUILD.gn:177–181 通过 generated_file 写空 sysroot 的 .empty。
它们是正常的 GN 配置期产物，Ninja 中没有相应运行时产出边并不表示 GN 模板有缺陷。

本轮问题是复制了部分配置结果，却没有带齐 GN 已生成的目录内容。不能据此推出
“只有改 GN 模板或建立外部输入契约两种办法”。后续应优先验证在新目录完整运行 GN，
或复用一份来源一致的完整 GN 配置快照，而不是逐个伪造占位文件。

## 4. CXX 目标的规模边界

真实对象路径为 obj/content/shell/app/libcontent_shell_app.shell_main_delegate.o。
-t commands 列出 9722 条传递命令，-t inputs 列出 55384 行输入；其中有 4477 条 ACTION、
2657 条 host CXX、183 条 host CC、1699 条 GTOS CXX 等。
这是传递命令清单，不是一次成功完成的 dirty-edge 调度计划；不能断言任何增量状态下
都必然执行全部 9722 条，也不能说编译器前端诊断必须先执行这些命令。

031 的完整对象 -n 在缺少 GN 配置生成的 .empty 时终止，因此没有完成有效调度。
本轮没有执行 GTOS CXX、链接或浏览器，拒绝超出试点规模的整体构建是正确的。

## 5. 资源与未取得的隔离证明

object-plan 实际 472.150 秒，超过授权单次 120 秒；后台 job 的 UI 等待不是操作系统超时。
早期若干调用事后补记，原记录保留，不追认为启动前登记。
报告中工具合计 840.261 秒，复制约 470 秒是估计，二者相加约 1310 秒，不能叫实测最坏上界。
单独的版本/工具查询与不完整 argv 记录应按其实际证据范围理解，不宣称全轮精准采样。

env -i 清理环境变量，不会关闭网络；图文件摘要未变不等于内核禁止来源写入；
shell 返回不等于量测过剩余后代为零。031 没有提供实际 network namespace、Landlock
拒写或 Linux 进程组扫除证据，本轮只接受观察到的生成与增量结果，不接受这些隔离保证。
后续复用现成 OS 隔离与超时原语，提供少量直接证据，不再扩展资格或监督框架。

旧 024 账本保持 436 行 / 422111 B，SHA256
f18af0dec989b8ddb0e677bdc0c7cab6414ff6ea9b73ee965e13a2aa4e09b3bd，未追加。

证据在 artifacts/native-ninja-route-validation-20261022-a：report-031.md、manifest-031.json、
runs-031.jsonl、logs/build-02.txt、logs/noop-01.txt、trial-01/build/.ninja_log 及对应目标文件。

本轮 GN 再生成、M、CXX、链接、content_shell/浏览器、kernel/ISO/guest/display/push 均未运行。
原生 Chromium 从未运行，32×32/4096 表面限制未变。
