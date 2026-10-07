#!/bin/bash
# Command-scoped official Ubuntu sources avoid the runner's stalled HTTP mirror.
set -euo pipefail
if [ "$#" -eq 0 ] || [ -z "${RUNNER_TEMP:-}" ]; then
    echo 'Usage: RUNNER_TEMP=... install-ubuntu-tools.sh PACKAGE ...' >&2
    exit 2
fi
source /etc/os-release
if [ "$ID" != ubuntu ] || [ "$VERSION_ID" != 24.04 ]; then
    echo 'This acceptance installer requires the configured ubuntu-24.04 runner.' >&2
    exit 2
fi
sources="$RUNNER_TEMP/gtos-official-ubuntu.sources"
cat > "$sources" <<'SOURCES'
Types: deb
URIs: https://archive.ubuntu.com/ubuntu/
Suites: noble noble-updates noble-backports
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: https://security.ubuntu.com/ubuntu/
Suites: noble-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
SOURCES
options=(-o "Dir::Etc::sourcelist=$sources" -o 'Dir::Etc::sourceparts=-'
    -o 'Acquire::http::Timeout=30' -o 'Acquire::https::Timeout=30'
    -o 'Acquire::Retries=2' -o 'Acquire::Languages=none')
sudo apt-get "${options[@]}" --error-on=any update
sudo apt-get "${options[@]}" install -y "$@"
