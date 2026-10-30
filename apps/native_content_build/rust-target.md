# C 前沿：真实 GTOS x86 Rust 目标诊断（r05）

本文件只记录**诊断与所需的真实输入**，不宣称任何 Content 配置、编译或链接通过。C1/C2/C3 的定义保持 issue #17 原样：C1=正式 `shell_main_delegate` 对象编译及 MD 通过；C2=真实依赖闭包链接产物；C3=该产物进入 GTOS 客体。当前最远仍只到 GN 首错。

## 1. 当前真实首错与它的成因链

命令与日志见 `L/tmp/content-154-gn/`（r05 保全副本在 `R/evidence/native-preserved-154-r05/c1-gn/`）：

```
gn gen --root=<view> out/gtos-content-bringup \
  --root-target=//content/shell:content_shell_app \
  --args='target_os="gtos" target_cpu="x86" gtos_frontier_bringup=true'
→ ERROR at //build/config/rust.gni:348:3: Assertion failed: assert(rust_abi_target != "")
```

成因链（逐条有源）：

1. `//build/config/BUILDCONFIG.gn` 里 `is_gtos = current_os == "gtos"`，`current_os` 来自 `target_os`。
2. `//build/rust/rust_static_library.gni` 被 `//content/shell/BUILD.gn:11` 无条件 import，`build/config/rust.gni` 因而必然参与。
3. `rust.gni:139/148/170` 三档 `toolchain_has_rust`：默认 false，但 `custom_toolchain_supports_platform` 支路让它对自定义工具链为 true。
4. 本移植树的 `//build/toolchain/gtos/BUILD.gn` 只声明 `toolchain_args { current_os = "gtos"; current_cpu = …; is_clang = true }`，既没有 `toolchain_has_rust = false`，也没有配套 rust 工具链。
5. `rust.gni:202` 起按平台映射 `rust_abi_target`，**没有 gtos 分支**，于是保持 `""`，在 `rust.gni:347` 的 `if (toolchain_has_rust)` 内被 348 行断言拦住。

也就是说：首错不是"缺一个三元组名字"，而是**这条工具链声称支持 rust，却既没有 gtos 的 ABI 映射，也没有能消费该目标的 rustc/sysroot**。

## 2. 环境里真实存在的 Rust 工具（不是缺编译器）

`//third_party/rust-toolchain` 完整存在：

| 文件 | 字节 | sha256 前 16 |
|------|------|--------------|
| `VERSION` | 243 | `a3b613ececb9f283` |
| `bin/rustc` | 315608 | `c3ab5bcf076dedc1` |
| `lib/librustc_driver-cb2ac4eed9a6ff8f.so` | 234964312 | `efa7ccee52e73f2a` |
| `lib/libclang.so.24.0.0git` | 108051136 | — |

`VERSION` 内容：`rustc 1.100.0 1edd55dcfcd573872c727fa3e086369a71661ee0 (…-1120-llvmorg-24-init-7747-g62397f8b chromium)`。

`lib/rustlib/` 下有 `x86_64-unknown-linux-gnu`（宿主 std/rmeta）、`rustc-src`，以及 **`src/rust/library/`**——含 `core`、`alloc`、`compiler-builtins`、`panic_abort`、`profiler_builtins`、`std`、`stdarch` 等完整源码树。所以"用已有匹配版本源码在自有 L/S 构建该目标需要的 core/alloc/compiler-builtins"这条路是**可行的**，不需要下载。

## 3. 真实目标应该是什么

移植树的 C 工具链已经是真实三元素：`gtos_freestanding_toolchain("clang_x86")` → `triple = "i686-unknown-none-elf"`、`elf = "elf_i386"`，公共 flags `-ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -mno-mmx -mno-sse -mno-sse2`。

对应的 Rust 目标必须与这套 ABI 一致：**32 位、freestanding、无 SSE（软浮点）、无 redzone、panic=abort、静态重定位**。rustc 没有内置 32 位 x86 裸机目标（内置的裸机目标是 `x86_64-unknown-none` 等），因此需要一份**自定义 target spec JSON**（例如 `i686-unknown-gtos.json`），字段依据实际 clang 布局确定，而不是填一个 Linux 三元组。

## 4. 需要做的四步（尚未执行）

1. **target spec**：按 clang `i686-unknown-none-elf` 的 data-layout 写 `i686-unknown-gtos.json`：`arch: "x86"`、`pointer-width: 32`、`elf`、`panic-strategy: "abort"`、`relocation-model: "static"`、`disable-redzone: true`、`features: "+soft-float,-mmx,-sse,-sse2"`、`is-builtin: false`。
2. **构建 std**（在自有 L/S，不写共享树）：
   ```
   # build-std belongs to Cargo, not to rustc: rustc only takes --target <spec>.
   cargo -Z build-std=core,alloc,compiler_builtins -Z build-std-features=compiler-builtins-mem \
         build --target <spec> --release --offline --target-dir <private-dir>
   ```
   产物落到私有 sysroot 的 `lib/rustlib/i686-unknown-gtos/lib/`。
3. **GN 映射**（私有 candidate，最小）：`build/config/rust.gni` 增加 `is_gtos && current_cpu == "x86"` → `rust_abi_target = "i686-unknown-gtos"`；`build/rust/known-target-triples.txt` 增一行；为 gtos 增加一个真实 `rust_toolchain`（rustc 路径、sysroot、`current_os = "gtos"`），并接 `//build/rust/std`。
4. **再走完整原目标**：`gn gen` 完整依赖图 → `ninja` 执行 `shell_main_delegate.cc` 及 MD（这是 C1），再链接（C2）、进客体（C3）。

`enable_rust=false` 只作为明确标识的减配对照：它会让 `//build/rust/gni_impl/rust_target.gni:43` 的 `assert(enable_rust)` 失败，不构成无损解法，也不能代替 C1。

## 5. 本轮实际做到哪

- 已确认首错成因链、真实可用 rustc/rust-src、真实 C 工具链 ABI；已确认没有 Linux 三元组冒充的空间（映射必须配合 spec 与 sysroot）。
- 旧的 GN 首错记录与 r04 的 `REPLAY ok` 结果都保留原样，`REPLAY ok` 只说明重放合同通过，不表示 Content 配置或编译成功。

### r16 更正与新增（上面“未执行第 4 节第 1–4 步”已过时）

- **spec 已写**：`i686-unknown-gtos.json`，edition 2024（当前 `library/` 源码要求 2024；`compiler_builtins` 同样要求）。Cargo 要识别 `.json` 目标还需要 `-Zjson-target-spec`。`rustc --print target-libdir --target <spec>` 指向 `.../bin-rust/lib/rustlib/i686-unknown-gtos/lib`；目录不存在正是此前 `E0463` 的直接原因。
- **目标 std 组件已真实构建**：`libcore.rlib` 66,884,068 B、`libcompiler_builtins.rlib` 1,933,742 B；`library/` 源码（约 83M）复制进 `bin-rust/lib/rustlib/src/rust`，供 `rustc` 的 `sysroot` 解析。
- **探针已真实编译**：`#[unsafe(no_mangle)]` 的 `gtos_rust_probe.rs` 编译出 `gtos_rust_probe.o` 1128 B `a08eab0b0e3a70c7…`、`libgtos_rust_probe.rmeta` 3193 B `24a610dc59ab3db2…`（stage `c4-content-154-34`，rc 0）。这只证明目标 sysroot 可用，不证明 ABI、不证明 Chromium 可链接。
- **仍未做到**：Cargo `-Z build-std` 路径被缺失的离线 `rustc-literal-escaper`（`proc_macro`/sysroot）挡住，与上面直接构建 `core` 的路径是两件事；GTOS Rust 的 GN 映射与 sysroot 接线、`target_os="gtos" target_cpu="x86"` 下 `//content/shell:content_shell_app` 的 GN/Ninja 构建都未取得，**C1/C2/C3 仍全部未取得**。

### r16 追加：C ABI 消费方与运行库 C 名字（已真实测过）

- **C 调用方已真实链接**：`apps/native_content_build/rust/abi/{abi_export.rs,consumer.c,link.ld}` 由 `tools/build-rust-abi-probe.sh` 构建，`tests/rust_abi_probe_test.sh` 断言两次构建字节一致、无未定义符号、ELF32/i386/EXEC、入口 `0x40000000`、无解释器、结构体布局两侧同断言（8 字节，偏移 0/4/6）、三个真实调用点、无 x87。**整数/指针/结构体边界通，float 边界本身是下一项证据的分界**。
- **float 边界两次测量**：用目标默认 `libcompiler_builtins.rlib` 链接失败，报 `__addsf3`、`__gesf2`、`__gtsf2`、`__fixunssfsi` 未定义；`weight * 2.0` 折成加法，所以 `__mulsf3` 不在这一组里。
- **成因已定位并解决**：`compiler_builtins` 的 C 名字在 `unmangled-names` feature 之后（`library/compiler-builtins/compiler-builtins/Cargo.toml:58`）。rustc 可直接用 `--cfg 'feature="unmangled-names"'`（外加 `compiler-builtins`、`arch`）从 toolchain 自带源码构建，**不需要 Cargo、不需要网络、不需要 vendored crate**：`tools/build-gtos-rustlib.sh` 做这件事，`tests/rust_builtins_test.sh` 断言 C 名字存在、两次构建字节一致、默认库没有这些名字（对照）、归档成员是 ELF32 i386 目标对象。同一链接在换上该库后成功，无未定义符号、真实调用 `gtos_abi_scale`、无 x87。
- **因此 Cargo `-Z build-std` 不再是运行库的前置条件**：运行库可用 rustc 直出。剩下的前置是把它接进 GN（`//build/config/clang/BUILD.gn` 的 `clang_lib("compiler_builtins")` 目前在 gtos 上 `assert(false) # Unhandled target platform`）。
- **GN 侧已推进到的最远点**（每一步都是实测首错，见 `R/c1-gn-rust-frontier-154-r16.md`）：`enable_rust=true` 下先缺 `rust_abi_target`（已在 port 的 `build/config/rust.gni` 增加 gtos 分支 → `i686-unknown-gtos`，并在 `known-target-triples.txt` 增行），再缺 `gtos_clang_bin`，再是上面的 `clang_lib` 平台分支。**仍未生成任何 object，不宣称 C1。**

