# GTOS

简体中文与 English 可在外观设置中切换，语言和主题会保存在专用磁盘镜像中。
启动器支持按字符编辑 UTF-8 文本和有范围限制的拼音候选词；这还不是通用输入法。
详见[语言支持范围](docs/desktop-localization.md)。

A 32-bit x86 educational operating system, developed on the `dev` branch.
The current foundation milestone includes a real 800×600 windowed desktop, hardware monitor,
validated memory allocators, page-level kernel protection, bounded multicore kernel workers,
a timer-driven BSP scheduler, safe dedicated ATA app
storage, and an externally packaged playable Catch game.

This is an evolving OS, not a production operating system. The current desktop supports movable/resizable windows, launcher search,
minimize/restore, appearance settings and installable apps. A legacy 320×200
fallback is retained. Bounded native ELF32 processes now have separate CPL3 address
spaces, checked system calls and recoverable user faults. General AP scheduling,
a full filesystem/POSIX runtime, persistent sessions and richer desktop services
remain explicit roadmap work.
See [the engineering roadmap](docs/ROADMAP.md).

## Build and run

Requirements: GNU g++/binutils with i386 freestanding support, GRUB i386-pc tools,
xorriso, mtools, Python 3, and QEMU x86. No C++ standard library or multilib libc is needed.

```sh
make GTOS.iso
./tools/run-qemu.sh
```

The runner creates a NEW dedicated 8 MiB app-store image at `data/apps.img` if it
does not exist, then keeps it across runs and build cleanup. It never attaches a host block device.
The kernel refuses to format arbitrary media. This experimental disk format is
not FAT/ext4 and must not be pointed at a valuable image or physical disk.

For a headless environment:

```sh
QEMU_DISPLAY=none ./tools/run-qemu.sh
```

QMP is available on standard input. Example commands after the initial greeting:

```json
{"execute":"qmp_capabilities"}
{"execute":"human-monitor-command","arguments":{"command-line":"sendkey i 100"}}
{"execute":"screendump","arguments":{"filename":"/absolute/path/desktop.ppm"}}
{"execute":"quit"}
```

The script supports `GTOS_LEGACY=1` for the VGA fallback, `GTOS_MEMORY=32M`, `GTOS_CPUS=4`, `GTOS_DISK=/path/to/image`,
and an optional sibling `gtos-runtime` rootless toolchain. The ISO boot is required;
QEMU direct `-kernel` is not the supported Multiboot launch path.

## Desktop and game

- Mouse focuses windows; drag titlebars, resize lower-right corners, or use title controls
- `1`/`H`: Home; `2`/`M`: hardware monitor; `3`: applications; `4`: appearance
- `L`: searchable launcher; `Tab`: cycle windows; `[` minimizes, `]` maximizes/restores
- In Appearance, choose English/简体中文 or press `C`; language and theme persist
- Chinese launcher: type pinyin, select1–9 or Space/Enter; backtick toggles direct input
- `I`: install the external Catch package from the boot ISO
- `Up`/`Down`: select an installed app; `Enter`/`G`: launch
- `U`: ask to remove the selected app; Enter confirms, Esc cancels
- In Catch: arrows or `A`/`D` move the paddle; catch falling blocks for points
- `R` or `Space`: restart the game; `Esc` or the window close button: return to apps

Installations and removals survive reboot. The game is bytecode in
`apps/catch.gtapp`, built from `apps/catch.json`; game logic is not built into the
kernel. See [the app protocol](docs/apps.md) and [storage format](docs/storage.md).

## Native userspace foundation

The boot ISO also carries two independently compiled ELF32 programs from
`apps/native/` plus the application-side `apps/browser_probe/` ABI fixture. They execute at CPL3 with different page directories and private
data at the same virtual addresses. One deliberately faults on a kernel-page
write; the peer continues and exits normally. The kernel checks both results and
requires the independent ABI fixture to exit0, then reclaims all three processes
back to the exact free-frame baseline before reporting
`NATIVE RUNTIME PASS`. The intentional `NATIVE USER FAULT` diagnostic is part of
this acceptance test, not an unexpected kernel crash.

The default is an experimental integer-only ABI with four process slots, bounded
image sizes, checked console writes, ticks, yield and exit. An optional
[legacy FP ownership profile](docs/native-fp.md) is separately tested and remains
disabled by default. Non-PAE paging has no NX; TLS, dynamic linking, general
files/threads and a browser runtime are not provided by this proof. Native ELF installation through the bytecode app store
is not claimed. See [process contract](docs/native-processes.md),
[process memory](docs/process-memory.md), and [ELF validation](docs/elf32-loader.md).

## Separate x86-64 foundation

An explicitly built [x64 target](arch/x86_64/README.md) now proves BIOS/GRUB
long-mode entry, four-level supervisor W^X mappings, NX/WP and guarded exception
stacks. Its [physical frame pool](arch/x86_64/FRAME_POOL.md) selects real available
RAM from validated boot information, excludes live allocations, zeroes frames,
and tests exact ownership/reuse and reclamation. The initial pool manages at most
8 MiB below64 MiB; this is a bounded service proof, not all available machine RAM.

```sh
make -f arch/x86_64/Makefile
python3 arch/x86_64/tests/boot_qemu.py --output /tmp/gtos-x64-boot-new
python3 arch/x86_64/tests/frame_qemu.py --output /tmp/gtos-x64-frames-new
```

A separately enabled [sparse VM core](arch/x86_64/SPARSE_VM.md) now reserves
large virtual intervals without proportional backing, commits zeroed pages,
protects R/RW/NONE, and decommits/releases with checked ownership and rollback.
Its tests reserve roughly 1.35 TiB while backing only selected pages; this is
virtual space, not physical RAM. Run the full guest gate with:

```sh
python3 arch/x86_64/tests/vm_qemu.py --output /tmp/gtos-x64-vm-new
```

This remains a BSP-only supervisor service with fixed metadata and commit limits.
It has no user ABI, desktop, threads, executable/JIT mappings or browser. Discard,
reset, trim, split and punch are not yet implemented. The working i386 desktop
and its test suite remain maintained.

## Verification

```sh
make test
python3 tests/desktop_qemu.py --output /tmp/gtos-modern-new
python3 tests/desktop_language_qemu.py --output /tmp/gtos-language-new
make GTOS-legacy.iso
python3 tests/qemu_smoke.py --iso GTOS-legacy.iso --output /tmp/gtos-legacy-new
```

The deterministic suite tests the actual i386 allocator/scheduler/VM/store code.
If the host cannot execute i386 Linux binaries, install official `qemu-user` and
make `qemu-i386` available on PATH. QEMU acceptance also uses Pillow for screenshot
and actual paddle-motion assertions. Use a fresh output directory each run.

Acceptance covers 32/64/128 MiB, one/four firmware CPUs, boot allocator checks,
real scheduler sleep/yield/return, GUI keyboard input, app install/launch/restart,
actual game paddle movement, and install/removal/reinstall across fresh VM boots.
The debug log and monitor distinguish detected CPUs, ready/busy/failed AP workers,
completed jobs and BSP-only general scheduling. Each ready AP repeatedly executes
a bounded integer job whose result and hardware APIC identity the BSP verifies. Safe AP startup is tested on one/two/four/eight CPUs;
missing-AP timeouts and no-APIC rejection are tested too. The worker pool additionally
verifies shared paging, cache compatibility, per-worker faults and continued BSP
timers. See [the worker contract](docs/cpu-work-pool.md).

The default build uses -O2. Use `make OPTIMIZATION=-O0` for an unoptimized build
after `make clean`. App data under `data/` survives build cleanup.

The BSP now enables real non-PAE paging: page zero is absent, kernel text/rodata
are read-only with CR0.WP, and device mappings are explicit. The bounded native
process layer adds separate user address spaces on top of that shared template;
the i386 non-PAE target still has no NX protection.

The Chinese atlas covers every localized interface/candidate string; arbitrary
Unicode/CJK coverage and a general-purpose IME remain future work.

More details: [language/input](docs/desktop-localization.md), [settings](docs/settings.md), [desktop](docs/desktop.md), [paging](docs/paging.md), [memory](docs/memory-management.md), [CPU/scheduler](docs/cpu-management.md).

The [native Chromium project](docs/BROWSER_PORT.md) is progressing through
verified kernel/API prerequisites and a separate Linux reference build. Chromium
and webpage rendering have not yet run inside GTOS.
