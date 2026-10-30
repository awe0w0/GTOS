#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 -m unittest discover -s tests -p 'qemu_runtime_test.py'
./tests/ata_wait_test.sh
./tests/kernel_integer_test.sh
./tests/memory_test.sh
./tests/memory_bootstrap_test.sh
./tests/live_storage_host_test.sh
./tests/boot_log_host_test.sh
./tests/paging_test.sh
./tests/process_memory_test.sh
./tests/native_process_test.sh
./tests/native_fp_test.sh
./tests/native_fp_qmp_test.sh
./tests/elf32_test.sh
./tests/i386_launcher_test.sh
./tests/host_publish_identity_test.sh
# The supply's own unit is the one unit whose two steps are separate processes rather than separate machines: prepare publishes,
# verify starts only what was published. A single host has no execution allow-list between the steps, so both run here with the
# supply's paths in a directory of this checkout's own.
mkdir -p out/host-supply/publish
GTOS_HOST_PUBLISH_ROOT=$PWD/out/host-supply/publish \
GTOS_PUBLISH_MANIFEST=$PWD/out/host-supply/manifest.txt \
GTOS_PUBLISH_RECORD=$PWD/out/host-supply/record.txt \
    ./tests/host_runner_test.sh prepare
GTOS_HOST_PUBLISH_ROOT=$PWD/out/host-supply/publish \
GTOS_PUBLISH_MANIFEST=$PWD/out/host-supply/manifest.txt \
GTOS_PUBLISH_RECORD=$PWD/out/host-supply/record.txt \
    ./tests/host_runner_test.sh verify
# The NativeRuntime unit had a staged interface and no caller, so its Create capacity, rollback and slot-reuse measurements had no
# recorded result. Build the image from the current sources, publish it read-only, then start only the published copy: the two
# selector refusals and all twelve cases, one case per process because a successful Activate publishes an owner with no destroy path.
mkdir -p out/runtime-unit/publish
./tests/runtime_test.sh --build out/runtime-unit > /dev/null
runtime_image=$(GTOS_HOST_PUBLISH_ROOT=$PWD/out/runtime-unit/publish \
    GTOS_PUBLISH_RECORD=$PWD/out/runtime-unit/record.txt \
    ./tests/runtime_test.sh --publish out/runtime-unit/runtime_test.elf | tail -n 1)
GTOS_HOST_PUBLISH_ROOT=$PWD/out/runtime-unit/publish \
GTOS_PUBLISH_RECORD=$PWD/out/runtime-unit/record.txt \
    ./tests/runtime_test.sh --run-all "$runtime_image"
# The surface unit was a single build-and-start script with no caller. It builds into a directory it refuses to reuse, publishes the
# four images, and then starts only the published copies, which is the same two-step shape every other measured staged unit has. The
# artifact directory is the one thing this block must not pre-create: the build refuses a directory that already exists, which is what
# running the suite end to end found.
rm -rf out/native-surface out/native-surface-publish out/native-surface-run
mkdir -p out/native-surface-publish
GTOS_HOST_PUBLISH_ROOT=$PWD/out/native-surface-publish \
GTOS_PUBLISH_MANIFEST=$PWD/out/native-surface-manifest.txt \
GTOS_PUBLISH_RECORD=$PWD/out/native-surface-record.txt \
    ./tests/native_surface_host_test.sh build out/native-surface > /dev/null
GTOS_PUBLISH_MANIFEST=$PWD/out/native-surface-manifest.txt \
GTOS_PUBLISH_RECORD=$PWD/out/native-surface-record.txt \
GTOS_HOST_PUBLISH_ROOT=$PWD/out/native-surface-publish \
SURFACE_BUILD_DIR=$PWD/out/native-surface \
    ./tests/native_surface_host_test.sh run out/native-surface-run
./tests/stage_contract_test.sh
./tests/browser_probe_test.sh
./tests/rust_abi_probe_test.sh
./tests/rust_builtins_test.sh
./tests/gtos_port_mapping_test.sh
./tests/suite_wiring_test.sh
./tests/rust_target_spec_test.sh
./tests/skia_alpha_probe_test.sh
python3 -m unittest discover -s tests -p 'browser_artifact_test.py'
./tests/keymap_test.sh
./tests/cpu_test.sh
./tests/cpu_startup_test.sh
./tests/cpu_work_queue_test.sh
./tests/cpu_memory_types_test.sh
./tests/package_vm.sh
./tests/i18n_test.sh
./tests/settings_test.sh
./tests/settings_sanitizer_test.sh
./tests/desktop_test.sh
python3 -m unittest discover -s tests -p 'package*_test.py'
python3 -m unittest discover -s tests -p 'storage_tool_test.py'
python3 -m unittest discover -s tests -p 'settings_tool_test.py'
./tests/storage_test.sh
./tests/storage_recovery_test.sh
echo "All deterministic module tests passed"
