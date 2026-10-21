# 原生 Content：video_capture 递归生成头闭包与新的 M 边界

023 的真实 Content `-M` 停在 `media/capture/mojom/video_capture_types.mojom-shared-internal.h`
缺失。024-r3 用一台真实 shared generator 补齐该头及六个配套产物，M 首错随之前移到
`ui/gfx/geometry/mojom/geometry.mojom-shared-internal.h`。024-r4 把这两个递归生成头
（`geometry`、`native_struct`）作为最小子图一次闭合，再取一次有效 M。

三个真实 ACTION 全部返回 0，31 项声明产物产生，28 项进入新 prefix。M 首错越过 r3 的两个缺失点，
落到闭包之外的新依赖 `media/mojo/mojom/audio_parameters.mojom-shared-internal.h`。
M = **RAN_RC1**：clang 真实启动一次、rc1、136.562 秒，这是诊断流程有效，不是 CXX 编译、
链接或浏览器运行成功。本次未提交、未推送。

## 1. r3 的已接受事实与未完成的闭包

r3 复用的七个 `video_capture_types` 产物按产生时回执保留，不重跑：
`mojom-data-view.h` 34167 B、`mojom-features.h` 470 B、`mojom-params-data.h` 809 B、
`mojom-send-validation.h` 18398 B、`mojom-shared-internal.h` 26689 B / `75499d1c…`、
`mojom-shared.cc` 54168 B、`mojom-shared.h` 49108 B。它们是 r4 的输入，不是 r4 的产物。

r3 的缺口不是“目录不对”，而是只闭合了目标 ACTION 的**输入**闭包，没有闭合它的**生成头**闭包。
`video_capture_types.mojom-shared-internal.h` 在第 14 行引入 geometry、第 16 行引入 native_struct，
这两个头 r3 都没有生成，所以 M 只能停在第一处。

## 2. 最小子图一次盘清

固定图未变：`native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`，
45456305 B，SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
只读解析缓存后，子图由三条边构成：

| 边 | 行 | ACTION | 声明产物 | 消费者 |
|---|---:|---|---:|---|
| parser | 13066 | `mojo/public/interfaces/bindings:bindings__parser_action` | 3 | bindings shared generator 的三个 module |
| generator | 13098 | `mojo/public/interfaces/bindings:bindings_shared__generator` | 21 | 21 个 bindings 头/源 |
| generator | 50415 | `ui/gfx/geometry/mojom:mojom_shared__generator` | 7 | 7 个 geometry 头/源 |

终止理由逐项可查：`geometry.mojom` 的 parsed module 只列它自己一条路径，没有任何 import；
`bindings.build_metadata` 的 `deps` 为空；parser 的 rsp 只列三个 IDL
（`interface_control_messages`、`native_struct`、`pipe_control_messages`）。所以这三个 ACTION
构成这两个根头在所查复用根下的生成头子图，不需要 normal / message-ids 一类无消费者的 ACTION。
生成头子图闭合与 parser 的源文件资格是两件事；后者的缺口见第 6 节。

三个 `.mojom-module` 在**声明复用根中未找到合格产物**——018、019、020、021、022、023 与 r3
十三个旧前缀根逐个按 logical 查找均无——因此由本轮 parser 真实产出，而不是跳过。
这不表述为“历史上从未生成过”。

四组必需集逐阶段冻结并实测：parser `{inputs 2, tools 1, reuse 1, runtime 170}`、
bindings `{3, 1, 2, 170}`、geometry `{4, 1, 3, 170}`。空组一律拒绝，而不是“零项通过”。

## 3. 一次生产与一个新前缀

三阶段按冻结顺序释放，全部 rc0：parser 2.126 秒 / 3 产物，bindings generator 5.542 秒 / 21 产物，
geometry generator 4.690 秒 / 7 产物。声明 31、产出 31；实际树独立枚举 38 项，
恰为 31 项声明产物加 7 项不同暂存文件（九个 stage 槽位、七个不同文件，`bindings.build_metadata`
与 `cpp_templates.zip` 各被两个 stage 暂存）。

暂存与产物分开记账：两个 `build_metadata` 与三个 rsp 是 GN 写出、没有 mojom ACTION 可引用，
按路径与摘要暂存并标注其真实权威；`cpp_templates.zip` 574664 B / `4763b1927c98…` 与
`geometry.mojom-module` 13017 B / `950acc48759b…` 从 interfaces 的 `run-02` 合格回执继承摘要；模板本身是该回执接收的复用项，
geometry module 则对应 `ui_gfx_geometry_mojom_mojom__parser_action` 的产出记录。
这些都不是本轮产物，也不计入 prefix。

28 项 generator 输出进入新 prefix；parser 的三个 `.mojom-module` 是生成输入，按 018 §5.3
不进 prefix。新 prefix 与旧 `1431 + 7 = 1438` 成员逐个 logical 全量比对：28 项在十三个旧根下
命中 0 次，既无同字节重复，也无不同字节或不同类型的冲突。它们落在两个旧前缀都不写的目录
（`mojo/public/interfaces/bindings/`、`ui/gfx/geometry/mojom/`）下，不存在靠前缀顺序静默遮蔽。

## 4. 一次有效 Content -M 与新的边界

基线取 r3 真正执行过的那次 M 回执（330 项 argv），不是更早的 329 项。本轮 argv = 330 + 1 = **331**，
新 `-I` 位于 r3 prefix 之后、`-I../source` 之前（token 定位，插入点 125）。移除新前缀后
差异为基线坐标 `[212,328]`，只有 crash token 与 `-MF` 值变化，没有新增 `-MG`，
没有修改宏、目标或优化；翻译单元仍为 `content/shell/app/shell_main_delegate.cc`，
target 仍为 `--target=i686-unknown-none-elf`。旧 1438 个成员逐个对各自独立固定产生时回执复读通过
（4/250/9/5/170/5/25/5/190/60/708/7，合计 1438）；28 个新成员与 24 个目标头在真实 include 根中
解析且字节一致。路径与字节一致不等于编译器读取了全部成员。

clang 真实启动一次，136.562 秒，rc1，854 B 日志只有一条错误：

```
In file included from ../source/content/shell/app/shell_main_delegate.cc:36:
In file included from ../source/content/shell/browser/shell_content_browser_client.h:15:
In file included from ../source/content/public/browser/content_browser_client.h:84:
In file included from ../source/third_party/blink/public/common/mediastream/media_devices.h:14:
In file included from gen/third_party/blink/public/mojom/mediastream/media_devices.mojom-shared.h:23:
gen/third_party/blink/public/mojom/mediastream/media_devices.mojom-shared-internal.h:15:10:
  fatal error: 'media/mojo/mojom/audio_parameters.mojom-shared-internal.h' file not found
```

r3 的错误位于 `video_capture_types.mojom-shared-internal.h:14`，该头第 16 行还引入
`native_struct`。本次 clang 走完这棵子树，返回上一层后停在
`media_devices.mojom-shared-internal.h:15`。这两个第 14/15 行属于不同文件，
不能当作同一文件的相邻行。旧缺失点已越过；新缺失点是**本轮未声明的依赖**，不在冻结子图内。

按“首个有效 M 移到冻结闭包之外的新依赖或 rc0 就停”，本轮在此停止：不追加生成
`audio_parameters`，也不启动第二个 clang。入口判定码 0 表示诊断已被记录并满足该次入口判定；
clang rc1 仍是 M 失败。`media_devices.mojom-shared-internal.h` 第 14–20 行还列有
`media_types`、`mojo/public/mojom/base/time`、`capture_handle_config` 三个直接生成头依赖。
它们未纳入本轮范围；下一轮应与 `audio_parameters` 一并判定合格复用或必要生产，
不能据本次首错断言它们在所有历史视图中均未生成。

## 5. 可定位证据

仓库外证据根为
`F:/GTOS-Chromium/artifacts/native-video-capture-mojom-frontier-20261021-a`，
下表相对该根的 `revision-r4/`。仓库自身不含生成物、固定图与受控入口，不能仅凭本文件独立复现。

| 对象 | 路径 | bytes | SHA256 |
|---|---|---:|---|
| 清单 | manifest-024-r4.json | 46211 | 6d0cdf0fd25f594e57d0ce9d2914111c4f0d1f12360256ff2ba9fb8ba8f56b6c |
| 资格回执 | qualification-024-r4.json | 9804 | e0bf32022935b8cb8d16b36df3782a6e99bfdbbe86a8a3cb82c80bc30727fb72 |
| 准备闭包 | out/out-024-r4-prepare-03/closure-024.json | 247012 | ec7cc2a6f63a2ad7ef8ca82b64d78d7e37a31f9df9ff89c7044fbc2351cb8fbb |
| 生产回执 | out/out-024-r4-produce-02/status.json | 542454 | c29e731ef7022b1b95f4245bdd480c020df2a25c4ac19cb3fef26a301e05c852 |
| M 回执 | out/out-024-r4-M-01/status.json | 84291 | 50636fe1abe43cfeba5168e0fd6865919f0899d7367aeb2d862a9adb0202c615 |
| M 日志 | out/out-024-r4-M-01/delegate-M.log | 854 | 6d91c7a3e6efb7f1ce3ca211b07296c9a9fecd926552065e45df822e71636b55 |

受控入口与计划（同目录）：`gate-024-r4-v2.ps1` 100761 B / `7b0abc78…`、
`parent-024-r4-v1.ps1` 27903 B / `4635063a…`、`entry-024-r4-v1.py` 60703 B / `bb880441…`、
`worker-024-r4-v1.py` 109326 B / `4c5a8607…`、`plan-024-r4-v1.py` 40262 B / `56e84779…`、
`cases-024-r4-v3.ps1` 78430 B / `2cbce6a7…`、`inputs-024-r4-v2.json` 10014 B / `a7ee765d…`、
`roles-024-r4-v7.json` 9333 B / `25ec7509…`。每次运行的源码快照随 OUT 保存并与回执绑定；
同名历史数据的正确匹配源是相应 OUT 快照，不是原路径。

## 6. 执行纪律

截止在**真实释放点**采样：租约以整数毫秒向下取整，再减去向上取整的预留与声明的交接借记，
余量为 0 即拒绝启动；跨 WSL 的交接耗时被实测并与声明值比较，超出即失败。这一条由生产路径自己的
反例证明：一次准备在声明 2.0 秒时实测 4.6659457 秒，被 `handoff_exceeded` 判失败；把声明值提到
10.0 秒（更大的借记只让子截止更早，属收紧）后，同一路径实测 2.28–4.95 秒并通过。末次复读与
长 postcheck 的最终结果受同一截止裁决，越线即失败。交接超支目前是在子进程结束后判定：
上述准备反例的 child 已返回 0，父壳才封为 4。这没有证明超支时业务从未启动，
也没有证明每项长扫描都能在中途截止；执行前拒绝仍是后续控制修正项。

024-r3 曾有七次未 claim 探针（三次集合转换、一次读资格书、三次直接调用资格判定函数）；
r4 短注补记它们，不追认、不倒补 claim、不修改旧证据。r4 早期的 gate v1 与 cases v2
也曾在运行后同名修改；历史运行匹配各自 OUT 的源码快照，不能称整个 r4 都已按新文件名冻结。
最终资格、生产与 M 的匹配快照保留。个别旧文件名与注释残留不决定运行身份，真正绑定的是角色摘要。

输入资格另有一个实质缺口：本轮 bindings parser 实际读取其 RSP 所列的三个 IDL，
但 `closure-024.json` 的 `idl_evidence` 为空；这三个源也不在 inputs、runtime 或 consumption 表中。
因此 31 项产出与 M 越过旧错误点是运行事实，不能据此声称 parser 的真实源输入已全部绑定。
后续复用须先补三源的独立摘要权威与消费检查；如需重新产生 module 作比较，记为新的验证，
不回填成本轮当时已完成的资格。

账本累计 26 对 claim/close/candidate/seal，封账计费 703.111 秒，r4 新增 12 对、334.786 秒。
GN ACTION 标签及实际启动累计为 4/4（r3 一条、r4 三条）；GN 图再生成未运行。

未运行：CXX 编译、`-MG`、链接、content_shell、原生 Chromium、网络服务、GN/Ninja 再生、内核、
ISO、guest、display。未扩 ABI/宏/SDK/权限，未删除任何旧 OUT 或失败证据。
