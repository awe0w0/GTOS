#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./tests/kernel_integer_test.sh
./tests/memory_test.sh
./tests/paging_test.sh
./tests/process_memory_test.sh
./tests/native_process_test.sh
./tests/native_fp_test.sh
python3 -m unittest discover -s tests -p 'native_fp_qmp_test.py'
./tests/elf32_test.sh
./tests/browser_probe_test.sh
python3 -m unittest discover -s tests -p 'browser_artifact_test.py'
./tests/keymap_test.sh
./tests/cpu_test.sh
./tests/cpu_startup_test.sh
./tests/cpu_work_queue_test.sh
python3 tests/cpu_memory_types_test.py
./tests/package_vm.sh
./tests/i18n_test.sh
./tests/settings_test.sh
./tests/desktop_test.sh
python3 -m unittest discover -s tests -p 'package*_test.py'
python3 -m unittest discover -s tests -p 'storage_tool_test.py'
python3 -m unittest discover -s tests -p 'settings_tool_test.py'
./tests/storage_test.sh
./tests/storage_recovery_test.sh
echo "All deterministic module tests passed"
