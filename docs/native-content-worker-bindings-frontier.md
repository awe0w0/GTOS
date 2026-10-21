# 原生 Content：worker 完整绑定头的阶段诊断

030 的三条 normal Mojo 生成边成功，完整产出 20 个声明文件。一次真实 Content 依赖扫描
越过此前缺失的 integrity_policy.mojom.h，在该头第 25 行因缺少
services/network/public/mojom/integrity_policy.mojom-features.h 停下。

正确分类是 **M = RAN_RC1**。原始 status.worker.actions[0].returncode 为 1，日志有一条
fatal error；外层 worker 进程和入口返回 0，表示诊断流程及后验按约定完成，不能据此写成
RAN_RC0 或“依赖扫描通过”。原生 Chromium 从未运行，CXX、链接和浏览器运行均未在本轮完成。

## 1. 固定配置与实际产出

图为 gn-clipboard-01/build/toolchain.ninja，45456305 B，SHA256
a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175。
单元为 content/shell/app/shell_main_delegate.cc，triple 仍是 i686-unknown-none-elf。
没有 GN 再生成、-MG、新宏、SDK 或 ABI 更改。

| normal ACTION | rule / build 行 | 声明并产出 |
|---|---:|---:|
| //services/network/public/mojom:mojom_integrity_policy__generator | 15998 / 16003 | 5 |
| //third_party/blink/public/mojom:tokens__generator | 22106 / 22111 | 5 |
| //third_party/blink/public/mojom:script_type_mojo_bindings__generator | 21047 / 21052 | 10 |

三条边各执行一次、各自 rc0。20 个物理文件逐项匹配生成时摘要，声明集合与固定图一致。
本轮前缀只选入 integrity_policy.mojom.h、tokens.mojom.h 和 script_type.mojom-forward.h
三项；其余 17 项保留在生产目录，不能据“未选入”推断它们不被递归需要或必然冲突。

输入按阶段计 5+45+6=56 次暂存，按 logical 去重为 54 项，其中 47 项引用已接受生成记录，
7 项为 GN 写出的 metadata/rsp，仅记录当时观察摘要。这两个计数层次不能混用。

## 2. 冻结闭包的实际边界与不足

本轮从 shared_worker_info.mojom.h 的可见生成头 include 出发，冻结 104 项：101 项已供、
3 项当时未解析。已供项中 37 项位于旧 overlay，64 项由基础 -Igen 提供。基础视图的
本轮字节核对不自动构成历史生成资格。普通源码头 40 项单列，未继续展开。

这是含未解析叶子的**局部图**。三份新头产出后，本轮没有继续展开它们的 include、重新验证
最终递归闭包；因此没有达到 030 任务书要求的“产出后最终闭包无未解析生成头”。
不能以事前局部图不含 features.h 为由声称没有漏选或完整绑定闭包已经闭合。

原始诊断为：

```
.../frame/policy_container.mojom.h:32
<030 overlay>/services/network/public/mojom/integrity_policy.mojom.h:25:10:
fatal error: 'services/network/public/mojom/integrity_policy.mojom-features.h' file not found
```

它证明本轮 integrity_policy.mojom.h 被实际读取且此前缺失点越过，不证明全部 20 项被读取。
缺少的 features.h 已在 026 的 shared 生成回执中产出（498 B，SHA256
f00ae20cb1d60e90012e621b243cb01fc4cdfc5348b85b6717fd85ce0d410608），
不是必须重新生成的文件。本轮还产出了未选入的 integrity_policy/tokens forward 头；
这些现成文件应在后续整体构建视图中评估，不能机械地每次补一个后缀再跑 M。

## 3. 计划与真实退出码

真实 argv 337→338，新 -I 位于 132，紧跟 029 prefix、在 -I../source 之前。
移除它后是 337 项等长数组：335 项相同，另两项仅 crash 和 -MF 输出值不同；
差异 [219,335] 为基线坐标，下标移动和值变化分开记录。
19 组 1800 个旧前缀成员按各组回执核对，新三项碰撞为零。
这些门通过只证明计划内的文件与顺序符合冻结声明，不证明冻结声明包含全部递归依赖。

原始 M 记录分三层：clang rc1；监管的 worker 进程 rc0；entry final.code=0。
日志 1103 B，SHA256 72613ac2a28075f4beee863965e8d164cbf182d935a20a2db4ebd5e6224f9605。
clang 一次、129.622761464 秒，未超时/被杀/异常，回收 remaining=0 为实测。

## 4. 耗时与账本

| 项 | 实际耗时（秒） |
|---|---:|
| 三个生产 ACTION 合计 | 12.677770901 |
| produce worker | 35.231020216 |
| produce entry | 40.236558007 |
| produce outer seal | 43.907 |
| M clang | 129.622761464 |
| M worker | 163.766127265 |
| M entry | 203.807659058 |
| M outer seal | 209.255 |

M 的 584.428 与 produce 的 1784.445 是获配时限，不是实际耗时。
原报告的 RAN_RC0、耗时与 argv 相同项数量均按原记录纠正，旧回执不改写。

本轮 9 个 claim/close/candidate/seal 全闭合，累计 109 对；账本 436 行、422111 B，
SHA256 f18af0dec989b8ddb0e677bdc0c7cab6414ff6ea9b73ee965e13a2aa4e09b3bd。
前 400 行原样保留；累计 seal 3594.930 秒、close 3578.298 秒。
分类累计 cheap26 / fixtures29 / prepare23 / produce18 / content13；ACTION30labels/45starts，clang9。
两次 content 入口中首次在计划门被拒，只有第二次实际启动 clang；不能把入口次数当 clang 次数。

## 5. 纪律与可复核入口

已运行的 plan 与 closure 被同名改写过，旧 OUT 和摘要按当次绑定保留，不追认为合规；
角色代码仍为固定已资格字节。三次 cheap 失败来自 job 字段不全，均留账。
原 plan 中 61/55/6 的散文未随执行常量 104/101/3 更新，不作结论权威。

证据位于 artifacts/native-video-capture-mojom-frontier-20261021-a/revision-r5/continuation-030：
produce 的 out-030-produce-01/status.json；有效 M 的 out-030-M-02/status.json 与 delegate-M.log；
各 job、source-snapshot、固定图和 ledger 分别记录来源与执行身份。
仓库文档只说明本轮已测事实；后续应核查现有 GN/Ninja 目标调度、完整生成产物视图与增量复用，
不能把“历史未运行 GN/Ninja”当作它不适用于 GTOS 的证据。

本轮未做 CXX、链接、content_shell/浏览器、network service、GN 再生成、ABI/宏/SDK、
kernel/ISO/guest/display。32×32/4096 表面限制未动。
