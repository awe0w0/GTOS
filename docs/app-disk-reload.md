# Reload the application disk from the modern desktop

A transient mount/read error or uncertain directory commit previously told the
user to remount, but the modern desktop provided no remount action. The user had
to reboot. Applications now offers **Reload disk** and the **R** shortcut while
that window is focused. The same controls work in the Chinese locale.

Reload calls the existing `AppStore::Mount()` validator. It identifies the
existing disk and reads its superblock and two directory snapshots. It never
formats media, writes packages, changes generations, flushes, or acknowledges
durability. The normal transaction recovery barrier still governs subsequent
mutations after an uncertain commit. Reload does not repair damaged payloads or
make cached writes durable.

The selected application is preserved by ID if it still exists. A failed mount
clears the directory listing and shows the existing localized error. Missing
and foreign media remain unavailable. The launcher treats R as text, a removal
confirmation consumes it, and a running game retains R as restart. The button
uses its drawn bounds for pointer activation and cancels previous capture.

## Reproduce

Use the normal supported compiler, dependencies and modern ISO:

```sh
make apps/catch.gtapp
GTOS_DESKTOP_SANITIZERS=1 ./tests/desktop_test.sh
./tests/storage_test.sh
./tests/storage_recovery_test.sh
./tests/i18n_test.sh
make GTOS.iso
python3 tests/app_disk_reload_qemu.py --output /path/to/new-output
```

The QEMU test exclusively creates its own images. It preinstalls the genuine
Catch package offline, reloads through real keyboard/mouse input, launches and
restarts the game, then compares every disk byte by SHA-256. It also boots a
persisted Chinese locale at 32 MiB/one CPU and rejects a foreign superblock at
64 MiB/four CPUs without modifying it. The Chinese settings bytes reuse the
existing settings-format fixture and are written only while the image is
offline. This test requires the existing QEMU/Pillow dependencies; an unpacked
QEMU needs `GTOS_QEMU_DATA_DIR` set to its real firmware directory.

The deterministic desktop test uses the real desktop, AppStore, package
validator, framebuffer and VM with a faultable block device. It proves temporary
read failure followed by recovery without restarting, missing/foreign-media
refusal, zero writes/flushes/generation changes, modal/launcher/game routing,
and preservation of the eighth selected application. It runs as freestanding
i386 and x86-64 binaries and under ASan/UBSan. This is component error injection;
the QEMU test does not inject a physical ATA error.

## Recorded acceptance

Qualified against dev `aa686a12a634712d77c9f13d7764e74661fbd186` on 2026-10-07.
The added recovery assertion fails against the unchanged baseline desktop and
passes with this implementation. Desktop i386/x86-64/ASan/UBSan, storage,
cached-remount recovery and localization suites passed. Fresh GCC 13 O0/O2
full kernels passed the existing linked instruction and source-interface
audits. All three real QEMU configurations passed; the original 800x600 Chinese
screenshot was visually reviewed.

Local evidence: `artifacts/app-disk-reload-20261007T083650Z` and
`logs/app-disk-reload-status.json` in the independent Chromium workspace. The
first QEMU attempt used an incorrect qualification-shell firmware path and
failed before guest boot; its log/status/image were preserved. Only the guest
checks were repeated with the existing correct private firmware directory.

Qualified O2 kernel SHA-256:
`4da166ebec7c8d1d269051bea3f11f6116e541dd4cc41368ec99d112c567d881`.

This change provides an integrated GTOS disk recovery operation. It does not
add a general filesystem, native application file API, browser profile storage,
V8 runtime, graphics submission, or Chromium browsing support. Process/VM/FP
implementation, ABI, ELF loader and the frozen M1/ELF64 work are unchanged.
