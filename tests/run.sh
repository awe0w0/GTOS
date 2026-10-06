#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./tests/memory_test.sh
./tests/paging_test.sh
./tests/cpu_test.sh
./tests/cpu_startup_test.sh
./tests/package_vm.sh
python3 -m unittest discover -s tests -p 'package*_test.py'
python3 -m unittest discover -s tests -p 'storage_tool_test.py'
./tests/storage_test.sh
echo "All deterministic module tests passed"
