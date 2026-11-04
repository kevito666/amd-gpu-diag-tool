#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Execute using: bash scripts/promote-v080-step1.sh /path/to/source.tar.gz
# This runs in a child shell; failures will not close the user's terminal.
set -euo pipefail

PKG="${1:-}"
ROOT="${AMD_DIAG_ROOT:-$HOME/src/amd-gpu-diag-tool}"
if [[ -z "$PKG" || ! -f "$PKG" ]]; then
    echo "Usage: bash promote-v080-step1.sh /path/to/source.tar.gz" >&2
    exit 2
fi
if [[ ! -d "$ROOT/.git" ]]; then
    echo "Expected existing Git checkout at: $ROOT" >&2
    exit 2
fi
cd "$ROOT"
if [[ -n "$(git status --porcelain)" ]]; then
    echo "Working tree has changes. Commit/stash them before promotion." >&2
    git status --short
    exit 2
fi
if [[ "$(git branch --show-current)" == "main" ]]; then
    if git show-ref --verify --quiet refs/heads/refactor/v0.8.0; then
        echo "Refactor branch already exists: switch to it manually." >&2
        exit 2
    fi
    git switch -c refactor/v0.8.0
elif [[ "$(git branch --show-current)" != "refactor/v0.8.0" ]]; then
    echo "Expected main or refactor/v0.8.0 branch." >&2
    exit 2
fi

# Guard against accidental extraction into .git/archive or parent directories.
if tar -tzf "$PKG" | grep -Eq '(^/|(^|/)\.\.(/|$)|^\./(\.git|archive)(/|$))'; then
    echo "Unsafe paths in package; source was not extracted." >&2
    exit 2
fi
if [[ "$(tar -xOzf "$PKG" ./VERSION 2>/dev/null)" != "0.8.0-dev.1" ]]; then
    echo "Wrong package version, expected 0.8.0-dev.1" >&2
    exit 2
fi
mkdir -p archive/snapshots archive/source-packages
stamp=$(date +%Y%m%d-%H%M%S)
snapshot="archive/snapshots/pre-v080-dev1-$stamp.tar.gz"
tar --exclude='./archive' -czf "$snapshot" .
tar -tzf "$snapshot" >/dev/null
cp -n -- "$PKG" archive/source-packages/
tar -xzf "$PKG" -C .
echo "Snapshot: $snapshot"
make clean
make -j"$(nproc)"
make check check-config check-parser check-pcie check-audit check-maya-nvme check-modules check-ui
printf '\nActive source version: '
cat VERSION
printf '\nGit status:\n'
git status --short
echo "Development checkpoint installed in local tree; not committed or pushed."
