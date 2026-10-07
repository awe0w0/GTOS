# GTOS live boot with BIOS and x64 UEFI

The live image uses an unsigned AMD64 GRUB EFI application to start the existing
i386 ELF32 Multiboot1 kernel. The same ISO retains its BIOS boot entry. All three
delivery menu entries pass `live`; EFI additionally passes `uefi` and requires
a supported GOP framebuffer before attempting legacy video setup.

Live boot locks all ATA Write28/WriteSector/Flush operations before storage use.
AppStore and SettingsStore use a real 261-sector RAM BlockDevice, with the
normal checksums, installation, reload and settings semantics. The bundled
Catch package is installed into RAM at boot. Session changes disappear after
reboot. The GTOS live kernel does not identify or read an internal ATA disk;
firmware and GRUB may read devices while finding the boot medium. Logging adds
no disk persistence, filesystem, partition or host-device operation.

## Build and delivery

Build the normal artifacts with `make -j8 GTOS.iso`, then run
`python3 tools/build-uefi-live.py --output <new-absolute-ISO-path>`.
`make GTOS-live.iso` is an opt-in convenience target for a fresh output.
The builder refuses existing outputs and evidence directories. For another
build, select new paths; keep previous qualification artifacts intact.

An ordinary Linux toolchain needs grub-common, grub-pc-bin, grub-efi-amd64-bin,
xorriso and mtools. Rootless extracted toolchains can specify `--efi-modules`,
`--bios-modules`, `--grub-mkstandalone`, `--grub-mkimage`, `--grub-mkrescue`,
`--grub-script-check`, `--xorriso`, `--mformat` and `--mcopy` explicitly.
The evidence manifest records helper versions and hashes, ELF32/Multiboot1 and
PE32+ headers, BIOS/EFI boot entries, the GPT ESP, and FAT/ISO payload round trips.
Packaging success and guest boot qualification are separate fields.

The generated `UEFI-media` tree contains `EFI/` and `boot/` for a dedicated
existing FAT32 removable medium. Copy both directories to its root after
checking for conflicting files. The builder performs no host-device operation.
Deliver the ISO, complete UEFI tree, Chinese self-boot/logging guide, SHA256
records, qualification evidence, licenses and corresponding source in a ZIP
through Library. Verify the final file hashes after the logging changes.

## Three live menu entries

The default is `GTOS Live (RAM only)`. The other entries are
`GTOS Live (boot log; press any key after logs)` and
`GTOS Live (boot log + COM1; serial hardware required)`.
They add `bootlog`, or `bootlog serial`, while retaining `live` and the
same four boot module slots. None offers writable internal-disk boot.

With a validated GOP and the modern desktop, `bootlog` (or the kernel's
`verbose` alias) prints stage text on the real framebuffer and pauses before
the first desktop frame. `BOOT LOG PAUSED - PRESS ANY KEY FOR DESKTOP` is a
normal diagnostic hold, not a panic. A real PS/2 key resumes; that key and its
release are consumed. USB input available in firmware does not establish
working GTOS input. The default entry avoids this normal diagnostic hold.

## Loader and kernel diagnostics

Firmware failures before GRUB starts remain firmware-owned. Signature rejection
and device discovery are not a GTOS firmware log. GRUB console messages use
`[GTOS LOADER Lxx]`, independently of the kernel RAM buffer:

| Code | Loader operation or error |
| --- | --- |
| L01 | x64 EFI entry reached |
| L10 / L11 | Searching for / finding the live marker |
| L12 | Live configuration found |
| L20 / L21 | Loading ELF32 kernel / four boot modules |
| L22 / L23 | Optional COM1 enabled / unavailable |
| L30 | About to hand off to the ELF32 kernel |
| L02 / L03 | Kernel / boot module loading failed |
| L04 | Kernel handoff returned without boot |
| L05 | EFI GOP or BIOS VBE loader module unavailable |
| L90 / L91 / L92 | Marker missing / configuration missing / configuration returned without handoff |

Loader failures remain on a console error screen. `GTOS LOADER STOP` refuses
kernel handoff and waits for power-off or reset; its quiet wait does not resume a
successful boot. Photograph the error before retrying. GRUB messages are not
copied into the kernel's buffer.

The kernel marks the beginning of each stage, rather than claiming that the
stage has finished:

| Stage | Initialization |
| --- | --- |
| B01 | Handoff into `kernelMain` |
| B02 | Multiboot memory validation, physical allocator and heap checks |
| B03 | GDT, IDT and scheduler preparation |
| B04 | RAM storage on live boot, normal storage otherwise |
| B05 | Graphics and desktop object preparation |
| B06 | Paging, protection and existing processor startup |
| B07 | PS/2 devices, PIT and interrupt activation |
| B08 | Desktop transition, including the optional diagnostic hold |

`DESKTOP READY` follows the hold/resume path. Kernel failure lines contain
`PANIC E<code> PHASE B<stage> <reason>`. Once a safe direct framebuffer is
registered, panic output preserves the reason and recent RAM log text. The
panic path uses bounded buffers and has no heap or disk dependency; recursive
failure halts instead of re-entering a failing renderer or UART. It does not
dump arbitrary memory.

`E01 INVALID MEMORY MAP` occurs before validated GOP registration and kernel
COM1 setup. Physical-machine screen or UART visibility is therefore not
promised for this early failure. An unsupported GOP (`E09`) also cannot promise
screen output; a successfully initialized optional COM1 may provide a channel.
Emulator E9 capture does not establish physical-machine visibility.

`E99 SAFE DIAGNOSTIC FAILURE` is an explicit qualification-only failure before
storage initialization. Normal delivery entries do not contain its
`bootlog-fail` token. The builder's diagnostic artifacts remain separate from
the deliverable image.

## RAM log viewer and serial capture

`gtos::common::BootLog` retains up to 8192 bytes in a fixed BSP buffer. Readers
preserve interrupt state. Older bytes are overwritten when full; `Dropped`
is a saturating overwritten-byte count. The oldest retained line can begin
partway through its original text. A final partial line is still readable.
There is no allocation, device I/O or persistence in this buffer.

Press `2` or `M` for the existing Monitor window, then `B` for boot logs.
`Up`/`Down` scroll retained lines; `B` or `Esc` returns to hardware information.
The view displays `BOOT LOG / IN RAM`, retained `Bytes`, overwritten `Dropped`
and the current first `Line`. Rendering uses one bounded `char[96]`; oversized
lines are shortened to the available width. Scrolling does not append to the
buffer. Reboot or power loss clears it. Photograph screens before rebooting;
there is no automatic log export to a disk or USB medium.

Only the serial entry requests kernel UART I/O. Physical GTOS serial capture
requires a real 16550-compatible COM1 at I/O `0x3f8`, a correctly connected
external receiver, and `115200 8N1`. A GTOS-side USB serial adapter is not
supported. UART polling is bounded; an absent or stalled device does not
create an infinite transmitter wait. The receiver must save any capture
externally. Logs preceding kernel UART setup are replayed from the retained
buffer after successful initialization; failures before that setup cannot
rely on this replay.

QEMU may capture its emulated UART and E9 sink to host files. E9 is an emulator
debug channel, not a physical-machine readable log or persistent guest file.
Optional GRUB COM1 output starts only when its serial menu entry is selected;
firmware and earlier loader failures are not guaranteed to reach that UART.

## Qualification history and final-image acceptance

Before the logging changes, `tests/live_storage_host_test.sh` passed real
RAM/AppStore/Settings checks at O0/O2 with sanitizers and actual i386 ATA
implementation checks with counted port access. The live latch rejected each
write/flush before port access.

The prior qualified kernel was
`c48575129359114d1c5cb873d8ef32cc8043b1bb2d1ba903e48f56b3eac30b50`.
Its eight real boots covered two boots per profile:

| Profile | Prior result and scope |
| --- | --- |
| OVMF PC, 128 MiB, 4 requested CPUs | Desktop/input/RAM operations pass; firmware inventory reported one CPU, AP=0, native runtime LIMITED |
| OVMF Q35, 128 MiB, 1 requested CPU | Desktop/input/RAM operations and native execution/reaping pass; AP=0 |
| BIOS live, 64 MiB, 4 CPUs | Live desktop, native runtime and AP jobs pass |
| Ordinary BIOS, 64 MiB, 4 CPUs | Positive disk-write control; package, Chinese locale and light theme persist after reboot |

The first three live profiles used writable private unknown/valid sentinel
disks. Whole-disk bytes remained identical and QMP reported zero writes.
The ordinary BIOS control recorded real writes. Exact payload/source hashes,
keyboard/mouse Catch pixel changes and reboot checks are in the historical
`uefi-final-qualification-20261007T133747Z/guest-proof-20261007T133747Z/results.json`.

The separate historical USB case used the complete UEFI tree on a 64 MiB FAT32
USB image, xHCI, and no CD-ROM. Firmware USB boot succeeded, followed by PC-mode
desktop/input/RAM, Chinese and theme operations. Both USB and ATA images retained
every byte with zero QMP writes; PC native execution remained LIMITED. This
qualifies the emulated firmware USB boot path, not GTOS USB/HID support. Evidence:
`uefi-usb-final-20261007T134215Z/results.json`.

The historical artifacts remain intact. The final logging image passed all
eight normal boots again: OVMF PC and Q35, BIOS live, and ordinary BIOS
persistence. Its complete UEFI tree also passed the separate FAT32 USB boot.
Live sentinel disks and USB bytes remained unchanged with zero QMP writes.
Q35 native execution/reaping and BIOS AP jobs passed; UEFI AP and PC native
execution remain explicitly unqualified.

The new six-case logging qualification passed phase ordering, RAM viewer
scroll/restore, actual PS/2 resume including held-key repeats, actual COM1
capture, serial-off checks, and E99 failure before storage initialization.
Separate missing-kernel and missing-module images reached visible L02/L03
console errors, refused kernel handoff, and retained byte-identical screens
after six real key presses. Root visual review confirmed the exact loader
errors, phase display, RAM viewer and E99 reason. The automated raw log result
retains its pending-visual-review flag; a separate SHA-bound review overlay
records the completed visual acceptance without altering that raw evidence.

Evidence: `uefi-bootlog-qualification-20261007T142152Z/` contains
`normal-eight-boots/results.json`, `boot-logs/results.json`,
`uefi-media-usb/results.json` and `visible-log-review.json`.
Build input/source hashes remained unchanged throughout these checks.
The bounded log host tests passed O0/O2 ASan/UBSan with 541,146 checks each;
the unchanged RAM/ATA implementation retains its qualified host-test hashes.

Final kernel SHA256: `8934e1fc83ffc07b75af42b6313171def508eafada4dd69c80d66d4204cd3728`.
Final normal ISO SHA256: `bcb67b2db0549da52565b6a72265c15aa6b47b044b1932e5e3f6d140f8e2c9e3`.
Final `BOOTX64.EFI` SHA256: `e9106cfe5c077e1eb6e41786c903d059e89564c9e295f117fc302b9a226ef0b5`.
The exact published source tree and automatic CI result are recorded in the
delivery metadata after the reviewed source commit is published to `dev`.

No frozen M1/ELF64 workload is part of this new logging acceptance. The normal
UEFI guest driver takes completed build evidence, an ordinary BIOS ISO, a fresh
output directory, the exact kernel SHA256 and repository-relative source hashes.
EFI defaults to qemu-system-x86_64 and BIOS to qemu-system-i386; explicit
`--qemu` and `--bios-qemu` overrides are recorded in command evidence.

## Hardware scope

Real hardware has not been tested. The EFI application is unsigned and Secure
Boot signing/qualification is unavailable; build and delivery do not alter
firmware settings, host partitions, security policy or boot entries.

Input uses 8042/PS2; USB HID, NVMe and AHCI drivers are unavailable.
UEFI requires a direct RGB32 framebuffer below 4 GiB, at least 640x480 and
at most 1920x1080, with an aligned pitch no greater than 16384 bytes.
Firmware keyboard support does not establish GTOS USB input support.
The live image never installs to or formats an internal disk.

PC OVMF's GOP at `0x80000000` conflicts with the existing native-process user
arena. Its desktop works, while native execution remains unqualified on that
profile. Q35 OVMF passed native execution/reaping on the historical image.
Both UEFI profiles lacked usable AP inventory; UEFI AP execution is still
unqualified. BIOS four-CPU worker results do not qualify UEFI AP or real
hardware. No paging, user arena, ACPI discovery or ELF64 contract is changed.

GRUB's [Multiboot implementation](https://raw.githubusercontent.com/rhboot/grub2/grub-2.06/grub-core/loader/multiboot.c),
[EFI handoff](https://raw.githubusercontent.com/rhboot/grub2/grub-2.06/grub-core/loader/i386/multiboot_mbi.c)
and [32-bit relocator](https://raw.githubusercontent.com/rhboot/grub2/grub-2.06/grub-core/lib/i386/relocator32.S)
describe the loader transition. Distribute its corresponding source and
license notices with bootable media, together with the existing font notices.
