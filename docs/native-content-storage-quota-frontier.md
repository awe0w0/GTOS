# 原生 Content：quota_client 完整头的生成与依赖扫描前沿

022 的真实 Content `-M` 停在 `storage/browser/file_system/file_system_context.h:30`，缺
`components/services/storage/public/mojom/quota_client.mojom.h`。023 运行必要的真实 GN ACTION，
产生完整头及本轮选定的生成头配套，再用同一翻译单元的 `-M` 验证原缺失点已越过。

22 个 ACTION 全部返回 0，730 个声明产物产生。本次 M 首错从
`content_browser_client.h:83` 引入的 storage 子树移到 `:84` 引入的 mediastream 子树。
M = **RAN_RC1**；这不是 CXX 编译、链接或浏览器运行成功。执行账本仅覆盖部分调用，
预算与命令纪律尚未通过整体审核，不能用生成成功推导整轮流程合规。

## 1. 真实 owner 与产物范围

`quota_client.mojom.h` 的 owner 是 `//components/services/storage/public/mojom:mojom`。
固定 GN 图在仓库外 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`，
45456305 B，SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
生成器按各真实 GN 边的完整声明集执行，没有裁成单个 IDL 或只写一个头。

| ACTION 类型 | 标签数 | 声明产物 |
|---|---:|---:|
| parser_action | 4 | 18 |
| type_mappings | 4 | 4 |
| shared_generator | 7 | 413 |
| generator | 7 | 295 |
| 合计 | 22 | 730 |

其中 `.h` 590 个、`.cc` 118 个、`.mojom-module` 18 个、映射表 4 个。
新 prefix 收入 normal/shared generator 的 708 个渲染文件，包括头和实现文件；
18 个 parser module 与 4 个 map 属于生成输入，不在 prefix 内。

| 新生成的完整头（`gen/` 下） | 字节 |
|---|---:|
| components/services/storage/public/mojom/quota_client.mojom.h | 6647 |
| components/services/storage/public/mojom/buckets/bucket_id.mojom.h | 7607 |
| components/services/storage/public/mojom/buckets/bucket_locator.mojom.h | 8932 |
| third_party/blink/public/mojom/quota/quota_types.mojom.h | 9721 |
| third_party/blink/public/mojom/storage_key/storage_key.mojom.h | 11050 |
| third_party/blink/public/mojom/storage_key/ancestor_chain_bit.mojom.h | 1288 |
| mojo/public/mojom/base/unguessable_token.mojom.h | 8110 |
| url/mojom/origin.mojom.h | 8335 |
| services/network/public/mojom/schemeful_site.mojom.h | 7706 |

目标头 SHA256 为 `2e57a64c812257c900e068dd889b83321a1708520404da7c4f5cb2051b522112`。
末端按各 stage 的生成时记录复读 730 项；声明集合与固定图一致，缺、多、空、非常规各 0。
审核另外核对了 730 个产生文件、708 个 M overlay 成员，摘要均与对应生产记录一致。
这些检查不证明 clang 读取过所有 730 项。

## 2. 输入、同轮前驱与覆盖边界

准备闭包 `out-023-prepare-02` 记录 996 项输入：843 项 `reused_stage_output`，153 项
`current_observation_only`，另有 170 项 runtime、3 项工具。runtime 和工具不加算在 996 内。
按准备回执的实际物理根，输入为 `a_u` 791 / `a_gn` 151 / `n23gen` 54；
声明但未取用的根单独记录，不能把来源根的声明或优先顺序当作实际取用计数。
18 个 root/import IDL 的字节摘要和 Git blob ID 与固定源提交
`04bca2584cddb39e134fc7c16bb2886e02863d0e` 的记录相符。

冻结表列出的 stage 暂存条目为 4544；实际 stage 回执中的暂存条目为 4450，约 397.3 MB。
条目可跨 stage 重复，按 logical 去重为 996。此处区分声明条目、实际暂存条目和不同输入数。

同轮前驱冻结输入列表有 82 条列举项，按每行 logical 去重后，释放门表有 54 条关系；
同一行内重复列举可以使列表计数增加，但不增加字典键数。逐行集合比较相等，
16 行显式为空、6 行非空。不能仅以“跨行重复”解释 82 与 54 的差值，因为跨行复用在两种
逐行计数中都会各计一次。

worker 在首次 ACTION 前比较冻结前驱表与固定图重算表，关系不一致时以
`not the set the pinned graph requires` 拒绝。30 个最终 fixtures 案例覆盖：
7 个文档形状负例、4 个关系负例、1 个列表/集合对照、5 个父门案例、13 个授权案例。
11 个释放前负例各使用自己的 spy，调用增量均为 0；父门正控在真实产物上核对 54 条关系。

完整 22 阶段 spy 正控的两次尝试均未在时限内完成，不能记作通过。
真实生产路径的 22 个 stage 按冻结顺序全部释放成功，提供实际运行的正向证据；
它不把未完成的独立 fixtures 正控变成通过，也不补成“生产前已完成全部集中案例”。
本轮实际生产和 M 在最终 fixtures 批次之前执行，该次序偏离了先完成集中演示的要求。

## 3. 一次真实 Content -M

argv 为 329 = 基线 328 + 一个新 `-I`，位于 022 prefix 之后、`-I../source` 之前。
移除新 prefix 后差异为基线坐标 `[210,326]`，仅 crash token 与 `-MF` 路径变化；
没有新增 `-MG` 或修改宏、目标、优化。旧十组 prefix 的 723 个成员分别按原回执复读。
708 个新成员与九个目标头在真实 include 根中解析且来源一致；“解析”指路径与字节验证，
不是声称编译器读取了全部成员。

clang 真实启动一次，129.741 秒，rc1；872 B 日志只有一条错误：

```
In file included from ../source/content/public/browser/content_browser_client.h:84:
In file included from ../source/third_party/blink/public/common/mediastream/media_devices.h:14:
In file included from gen/third_party/blink/public/mojom/mediastream/media_devices.mojom-shared.h:23:
gen/third_party/blink/public/mojom/mediastream/media_devices.mojom-shared-internal.h:14:10:
  fatal error: 'media/capture/mojom/video_capture_types.mojom-shared-internal.h' file not found
```

相同的有序 include 区段中，旧错误来自第 83 行引入的 storage 头，新错误来自第 84 行。
结合目标头在本轮 prefix 中的路径与字节验证，可以认定旧缺失点已越过；
不能扩大成所有宏配置、所有导入分支、所有生成文件或 TU 均已编译通过。
入口判定码 0 表示诊断已被记录并满足该次入口判定，clang rc1 仍是 M 失败。

新缺失头由固定图第 11848 行的
`//media/capture/mojom:video_capture_types_shared__generator` 声明，完整声明为 7 个产物。
该 ACTION 不在本轮 22 个标签中，其头也不在 730 项中。module 与 metadata 已在本轮输入中绑定，
这只证明这两项已具备，不能直接推导该生成器的所有其他输入和递归生成头均已闭合。
本轮未执行这个新 ACTION；后续生产需要新的授权。

## 4. 可定位证据与执行纪律

仓库外证据根为
`F:/GTOS-Chromium/artifacts/native-storage-quota-mojom-frontier-20261021-a`。
下表相对该根；仓库自身不含这些生成物、固定图和受控入口，不能仅凭本文件独立复现。

| 对象 | 路径 | SHA256 |
|---|---|---|
| 清单 | revision/manifest-023.json | dc4f249177a777484a47518308871c06f30e38cee03855113ec84edd3fc2e798 |
| M 回执 | out-023-M-01/status.json | efc12bef3b7ad9a155f16f53e57d8a0fd714dcd4db9f17d8a265d861fdc76176 |
| M 日志 | out-023-M-01/delegate-M.log | 1fefb7e129ea2903dd2ffeaac05d249ea914f47a5b423cb2f7ba51bc3215de58 |
| 原账本 | ledger-023.jsonl | 411e520210fcf9c50aaff0f998d1add55b69780f76257483b2d0ebbf5b159877 |

105 份受控 OUT 源码/job 快照与各自回执绑定一致。对成功生产与 M，应使用这些对应运行的
固定角色和回执；辅助构造器的同名改写不应与已冻结的生产角色混为一谈。

原账本有 16 对 claim/close、2648.540 秒，属于记录覆盖范围，不是完整执行总数或全轮耗时。
完整轨迹还包含两次无 claim 的早期固定入口失败，以及大量不经固定入口的 Python 构造、语法和
诊断调用。审核定位到 86 条含 Python 启动的 shell 调用、87 条启动语句；其中固定入口尝试为
18 条，另外 69 条是辅助启动语句。未把未采样时间补成实测值，也不补造旧 claim/close。
`out-023-preflight-03` 在 cheap 单次 120 秒授权下实际记录约 145.878 秒，不能称单次上限均满足。

另外存在禁止的 inline Python、源码写回形态、未冻结辅助脚本，以及非必要的 Codex 安装目录
搜索与嵌套 shell。生成与 M 事实继续保留，执行流程整体仍待返修，不因总墙钟有余而追认这些偏离。

Content CXX、`-MG`、链接、content_shell、原生 Chromium、网络服务、GN 再生成、内核、ISO、
guest、display 均未运行；32×32 / 4096 字节限制未改。
