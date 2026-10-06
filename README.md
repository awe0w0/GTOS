# GTOS

A 32-bit x86 educational operating system, developed on the `dev` branch.
The current foundation milestone includes a real VGA desktop, hardware monitor,
validated memory allocators, a timer-driven scheduler, safe dedicated ATA app
storage, and an externally packaged playable Catch game.

This is an evolving OS, not a production operating system. The current desktop is
a 320×200 foundation interface. Paging/process isolation, AP scheduling, a general
filesystem and a modern multi-window compositor remain explicit roadmap work.
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

The script supports `GTOS_MEMORY=32M`, `GTOS_CPUS=4`, `GTOS_DISK=/path/to/image`,
and an optional sibling `gtos-runtime` rootless toolchain. The ISO boot is required;
QEMU direct `-kernel` is not the supported Multiboot launch path.

## Desktop and game

- Mouse selects the Home, Hardware and Apps views
- `1`/`H`: Home; `2`/`M`: hardware monitor; `3`/`A`: applications
- `I`: install the external Catch package from the boot ISO
- `Up`/`Down`: select an installed app; `Enter`/`G`: launch
- `U`: remove the selected app from the dedicated image
- In Catch: arrows or `A`/`D` move the paddle; catch falling blocks for points
- `R` or `Space`: restart the game; `Esc` or the window close button: return to apps

Installations and removals survive reboot. The game is bytecode in
`apps/catch.gtapp`, built from `apps/catch.json`; game logic is not built into the
kernel. See [the app protocol](docs/apps.md) and [storage format](docs/storage.md).

## Verification

```sh
make test
python3 tests/qemu_smoke.py --output /tmp/gtos-acceptance-new
```

The deterministic suite tests the actual i386 allocator/scheduler/VM/store code.
If the host cannot execute i386 Linux binaries, install official `qemu-user` and
make `qemu-i386` available on PATH. QEMU acceptance also uses Pillow for screenshot
and actual paddle-motion assertions. Use a fresh output directory each run.

Acceptance covers 32/64/128 MiB, one/four firmware CPUs, boot allocator checks,
real scheduler sleep/yield/return, GUI keyboard input, app install/launch/restart,
actual game paddle movement, and install/removal/reinstall across fresh VM boots.
The debug log distinguishes detected CPUs, APs actually started/self-tested/parked,
and BSP-only scheduling. Safe AP startup is tested on one/two/four/eight CPUs;
missing-AP timeouts and no-APIC rejection are tested too.

The default build uses -O2. Use `make OPTIMIZATION=-O0` for an unoptimized build
after `make clean`. App data under `data/` survives build cleanup.

More details: [memory](docs/memory-management.md), [CPU/scheduler](docs/cpu-management.md).
