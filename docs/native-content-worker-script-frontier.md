# 原生 Content：worker 的 script_type shared-internal 前沿

027 的真实 Content `-M` 停在
`gen/third_party/blink/public/mojom/worker/worker_options.mojom-shared-internal.h:14`，
缺失的是 `third_party/blink/public/mojom/script/script_type.mojom-shared-internal.h`。
028 只做一件事：用这条边**自己的完整声明**把这一个头渲染出来，作为一个前缀成员追加到
027 的 335 项 argv 上，看首错是否越出 script_type 子图。

结果：一个 ACTION、**十四项**声明产物全部产生（rc 0），argv 由 335 变为 **336**，
该头在真实 include 根下解析，首错前移到
`gen/third_party/blink/public/mojom/frame/policy_container.mojom-shared.h:28`
引入的 `services/network/public/mojom/integrity_policy.mojom-shared.h`。
M = **RAN_RC1**：clang 真实启动一次、rc1、124.006 秒；这是诊断流程有效，
不是 CXX 编译、链接或浏览器运行成功。

## 1. 这一轮的边界

- 图未变：`toolchain.ninja` 45456305 B，
  SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
- 边只有一条：rule 第 21110 行、build 第 21115 行，
  `third_party_blink_public_mojom_script_type_mojo_bindings_shared__generator`，
  owner 为 `//third_party/blink/public/mojom:script_type_mojo_bindings_shared__generator`。
- 编译 triple 仍是 `i686-unknown-none-elf`，SDK、宏集合与图一律不动；
  没有新增 `-D`、没有 `-MG`、没有新增 ABI 或宏。本轮只新增一个 `-I`。
- 只新增 1 个 ACTION owner、1 次 ACTION 启动、1 次 clang 启动。
- 首错一旦越出 script_type 子图即停，不追下一族。

## 2. 这条边声明十四项产物，但只有一项是前缀成员

这条边不是"一个 module 渲染七项"，而是**一次 ACTION 渲染两个 IDL stem**：
`script_type` 与 `script_evaluation_params`。两个 IDL 都**没有任何 import**，
所以它们由同一条 rsp 一起交给生成器，十四项产物属于同一条边。

| 产物（`gen/third_party/blink/public/mojom/script/` 下） | 字节 | 本轮摘要 | 进前缀 |
|---|---:|---|---|
| `script_evaluation_params.mojom-data-view.h` | 3997 | `5f90f3c9…` | 否 |
| `script_evaluation_params.mojom-features.h` | 562 | `efa231a0…` | 否 |
| `script_evaluation_params.mojom-params-data.h` | 901 | `0528defc…` | 否 |
| `script_evaluation_params.mojom-send-validation.h` | 1810 | `af0e0b8f…` | 否 |
| `script_evaluation_params.mojom-shared-internal.h` | 3942 | `46fb482b…` | 否 |
| `script_evaluation_params.mojom-shared.cc` | 6382 | `3ce769ab…` | 否 |
| `script_evaluation_params.mojom-shared.h` | 11378 | `2652e6f7…` | 否 |
| `script_type.mojom-data-view.h` | 1624 | `beb01410…` | 否 |
| `script_type.mojom-features.h` | 510 | `7e342444…` | 否 |
| `script_type.mojom-params-data.h` | 849 | `d0ed18c6…` | 否 |
| `script_type.mojom-send-validation.h` | 1100 | `554c3f5e…` | 否 |
| **`script_type.mojom-shared-internal.h`** | **1548** | **`2f6cc0020e90955dccabd47cfdfc987cd1ad75c442c6f76fc93cd522c1448dfa`** | **是** |
| `script_type.mojom-shared.cc` | 1831 | `7fdc3f47…` | 否 |
| `script_type.mojom-shared.h` | 3080 | `04f9190a…` | 否 |

十四项全部产生并进回执。本轮选择集只提供当前缺失的
`script_type.mojom-shared-internal.h`，其余十三项保留为同一边的配套产物。
未选入不表示这些文件不能被其他 include 使用，也不表示选入一定发生冲突；
本轮实际选择集的逐 logical 碰撞检查为零命中。

边是**整条跑的**，不是切片：一条 ACTION 同时产出两个 stem，只报 shared-internal 一项
就不是图里声明的那条边。命令里两个转义敏感的实参按图原样保留，没有为了跑通而丢掉：

```
--export_attribute COMPONENT_EXPORT\(SCRIPT_TYPE_MOJOM_SHARED\)
--export_header base/component_export.h
```

上面是图中命令的 shell 转义写法；本轮实际 argv 中 attribute 的值是
`COMPONENT_EXPORT(SCRIPT_TYPE_MOJOM_SHARED)`，没有括号前的反斜杠。

## 3. 5 项输入：两项生成记录、一项已接受复用记录

build 行列出 69 个去重输入：64 个 `../source/` 文件、1 个 `.harddeps` phony、
**4 个 `gen/` 文件**。这 4 个加模板归档就是 5 项 staged 输入。

- **2 个 `*-module`**，两者都有已接受 `run-02` 回执（934 项产物）记录的生成时摘要：
  - `script_type.mojom-module` → `9df388be…`
  - `script_evaluation_params.mojom-module` → `bdfea5e5…`
- 1 个 GN 写的 `.build_metadata`、1 个生成器 `.rsp`：在整个图里**没有任何 mojom
  ACTION 产生过**，标注 `current_observation_only`，不冒充历史资格。
- 1 个模板归档 `gen/mojo/public/tools/bindings/cpp_templates.zip`，
  574664 B / `4763b1927c982adf683e7517d3fa650b9eea4f2629f4914e99fc37cbb070fe39`。

### 模板这次有了真实来源

027 把这个模板当作"没有回执字段的额外 staged 输入"。实际上已接受的 `run-02`
回执**确实**记录了同一个摘要——记在它顶层的 `reused_qualified_outputs`（48 项映射）里，
而不是 `outputs` 里。028 的冻结输入表明确记录该字段名及其历史摘要，
prepare 按表中固定的 SHA 核对实际模板字节，回执为 matches=true，
来源层记为 `reused_stage_output` / `run-02`。字段名是来源说明；
不能据此声称 prepare 在运行时重新查询并解析了 run-02 的那个字段。
这是**向前补齐**，不是往 027 回执里回填。

## 4. 闭包是两个头，其中一个早就在

`script_type.mojom`（281 B / `f7aa2fc8…`）与 `script_evaluation_params.mojom`
（1523 B / `f3ae1a0d…`）**都不 import 任何文件**。所以与 027 不同，这个闭包里
根本没有 IDL import 成员。

本轮真正写出的 shared-internal 头 include 了四个文件：

```
#include "mojo/public/cpp/bindings/lib/validation_errors.h"                     ← 源码树
#include "mojo/public/cpp/bindings/lib/native_enum_data.h"                       ← 源码树
#include "mojo/public/interfaces/bindings/native_struct.mojom-shared-internal.h" ← r4
#include "base/component_export.h"                                               ← 源码树
```

任务书提醒不要未加核查就把 `native_struct` 称作缺失。查了，**它已有现成渲染**：

| 头 | 归属组 | 该组回执记录的摘要 | 盘上 |
|---|---|---|---|
| `native_struct.mojom-shared-internal.h` | **r4**（5290 B） | `857f7398…` | 一致 |

目标 `script_type` 的 IDL import 闭包只有自身；`script_evaluation_params` 是同边配套词干，
不是它的 import。这里的生成头集合是 **2 = 1 本轮渲染 + 1 已由早前组提供**：
生成的 script_type 头直接 include `native_struct`，后者不来自这个 IDL import 图。
“0 复用”仅表示没有旧头被复制进新前缀，不表示本轮没有复用既有头、module 或模板。

它没有被重新提供，而是在**它自己所在的组里**按该组回执记录的摘要做正向断言。
实际解析仍遵循 `-I` 顺序；断言验证了该顺序下预期来源的字节，避免再供同名文件。

## 5. 统一 M 的实测变化

| 项 | 027 M（基线） | 028 M |
|---|---:|---:|
| argv 元素 | 335 | **336** |
| 插入位置 | — | 130（027 prefix 之后、`-I../source` 之前）|
| 新 prefix 成员 | 1 | 1（本轮）|
| 旧 prefix 组 | 16 组 / 1792 | **17 组 / 1793** |
| 成员解析 | 1 | 1 |
| 差异下标（基线坐标） | [216, 332] | **[217, 333]** |

移除新插入的 `-I` 后得到与基线等长的 335 项数组；其中 333 项相同，
另两项仅 `-fcrash-diagnostics-dir=` 与 `-MF` 的输出值不同。
下标移动和输出值变化分开记录，不能说仅删新 `-I` 就字节级还原基线。
旧 17 组每组都按**自己的**回执复核（`012` 4、`013` 250、`014` 9、`015` 5、
`016` 170、`018` 5、`019` 25、`020` 5、`021` 190、`022` 60、`023` 708、`r3` 7、
`r4` 28、`r6` 189、`025` 125、`026` 12、`027` 1），合计 1793。

逐逻辑的碰撞检查对着每个早前组**自己的**拼写表做，结果 0 命中。负向控制全部触发：
抬高 include 根被拒、篡改 flag 被检出、篡改 define 被检出、加 `-MG` 被检出。

## 6. 首错越出 script_type 子图

- 027：`worker/worker_options.mojom-shared-internal.h:14` →
  `third_party/blink/public/mojom/script/script_type.mojom-shared-internal.h` 找不到。
- 028：该行**通过**，首错变为
  `frame/policy_container.mojom-shared.h:28` →
  `services/network/public/mojom/integrity_policy.mojom-shared.h` 找不到。

到达路径变为
`shell_main_delegate.cc` → `content_browser_client.h` → `shared_worker_info.mojom.h` →
`shared_worker_info.mojom-shared.h` → `fetch_client_settings_object.mojom-shared.h` →
`policy_container.mojom-shared.h`。

`worker → script_type` 这一族已在前沿之后。新的前沿是 `services/network` 的
`integrity_policy`（经 blink `frame/policy_container` 到达），与本轮冻结的 script_type
子图不是同一条链。按任务书"第一份有效 M 越出冻结子图即停"，本轮到此为止。

## 7. 仓库身份与 HEAD 的说明（与本轮业务无关）

本轮 DSH **没有做任何 Git 写动作**，也没有 push 权限。执行期间用户另有一次授权，
由 Codex 完成仓库历史作者更正与推送，与本轮业务无关、不计入业务账本：

- 本轮作者更正完成时的采样：`dev` = `origin/dev` = 服务器 `dev` = `a697ea5d`；
- 原 HEAD `4a1f3ccb` 与新 HEAD 的 **tree 完全相等**，tracked 与 index 干净；
- 本轮所有已执行的 grant、role、job、回执与 M **均未因此改动**，也没有重跑。

今后获准的提交，author 与 committer 均为 `xiaokang wang <hanser0w0@gmail.com>`，
不再使用硬编码的 GTOS/DSH 身份；该要求本身不构成 commit 或 push 权限。

## 8. 没有做的事

未跑 CXX 编译、链接、`content_shell`/browser；未构建整个 Blink platform；
未重新生成 GN；未改 ABI、宏或 SDK；未跑 kernel/ISO/guest/display；未推送；
没有删改或改名任何旧目录、旧回执、旧 manifest；账本只由外层父进程追加。
原生 Chromium 从未运行，M 不构成浏览器可运行的证据。

## 9. 证据定位

以下路径均相对工作区中的
`artifacts/native-video-capture-mojom-frontier-20261021-a/revision-r5/continuation-028/`，
不是相对本 Git 仓库。脚本、回执和产物在仓库之外，本仓库自身不能独立复现这些运行。

| 对象 | 路径 | SHA256 |
|---|---|---|
| 冻结输入表 | `inputs-028-script-v1.json` | `afcce49f2ba6c9c6b753b1c3983330bfce649cffa0b168e5bc94d05028bda91c` |
| 真实闭包 | `out-028-prepare-01/closure-024.json` | `ef9913e5545cd3b90518d540a7062d2fdaa055a072dc3dcd4aa2d39fc153dff3` |
| 完整生产回执 | `out-028-produce-01/status.json` | `dc4b2fed915f6b6eeb32cd35d5379d05db8ff0e0e8d4a7e89778d053bd130f10` |
| M 回执 | `out-028-M-01/status.json` | `3b4c86175cfe67d1137420eddb307ef056d2d2eeb5f27d994661356fed362dbc` |
| 原始 M 日志 | `out-028-M-01/delegate-M.log` | `9f6d7fb57ccc53ae23d91ec0da4093a32b64d20bb499a48e5edd167b29ba7ee6` |
| M 计划 | `plan-028-M-v1.py` | `5bc036db6ba7c9b108122dc5680b5da3186d88c844c810efafcf276f7bf88971` |
| 声明/选择/碰撞/来源/闭包表 | `tables-028.json` | `3074f7efba761e9fa6be441dd1693978635a111f3c60d60501d4e9579666328e` |

单靠完整产物和前缀解析不能证明 clang 读取过目标；本轮还以真实 include 链
越过 worker_options:14、到达 policy_container 公开 shared 头的原始诊断作为推进证据。
不声称其余十三项产物也被 clang 读取；M 仍为 RAN_RC1。
