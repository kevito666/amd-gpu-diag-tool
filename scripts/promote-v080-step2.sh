#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run in a child shell: bash ./scripts/promote-v080-step2.sh PACKAGE
# Does not install, commit, tag, or push. Never runs `set -e` in the user's shell.
set -euo pipefail

PKG="${1:-}"
ROOT="${AMD_DIAG_ROOT:-$HOME/src/amd-gpu-diag-tool}"
[[ -f "$PKG" && -d "$ROOT/.git" ]] || { echo "Usage: promote-v080-step2.sh /path/to/source.tar.gz (existing Git checkout required)" >&2; exit 2; }
[[ "$(tar -xOzf "$PKG" ./VERSION 2>/dev/null)" == "0.8.0-dev.2" ]] || { echo "Wrong package version" >&2; exit 2; }

# Reject absolute/path traversal, symlinks and hardlinks in the source archive.
listing=$(tar -tzf "$PKG")
if grep -Eq '(^/|(^|/)\.\.(/|$)|^\./(\.git|archive)(/|$))' <<< "$listing"; then
    echo "Unsafe archive member name" >&2; exit 2
fi
verbose_listing=$(tar -tvzf "$PKG")
if grep -Eq '^([lh])' <<< "$verbose_listing"; then
    echo "Refusing source archive with links" >&2; exit 2
fi
cd "$ROOT"
branch=$(git branch --show-current)
current=$(cat VERSION 2>/dev/null || true)
case "$branch" in
    main)
        [[ -z "$(git status --porcelain)" ]] || { echo "Main has uncommitted changes: stop to avoid overriding them" >&2; git status --short; exit 2; }
        [[ "$current" == "0.7.0b" ]] || { echo "Expected clean public beta on main" >&2; exit 2; }
        git show-ref --verify --quiet refs/heads/refactor/v0.8.0 && { echo "Refactor branch already exists; switch to it first" >&2; exit 2; }
        git switch -c refactor/v0.8.0
        ;;
    refactor/v0.8.0)
        if [[ "$current" != "0.8.0-dev.1" && "$current" != "0.8.0-dev.2" ]]; then
            echo "Unknown refactor working tree version: $current" >&2; exit 2
        fi
        # Step 1 deliberately left uncommitted. It is backed up below.
        if [[ -n "$(git status --porcelain)" ]]; then
            echo "Existing uncommitted refactor changes detected. Snapshotting before replacement."
            git status --short
        fi
        ;;
    *) echo "Switch to main or refactor/v0.8.0 first (current: $branch)" >&2; exit 2 ;;
esac

mkdir -p archive/snapshots archive/source-packages
stamp=$(date +%Y%m%d-%H%M%S)
snapshot="archive/snapshots/pre-v080-dev2-$stamp.tar.gz"
tar --exclude='./archive' -czf "$snapshot" .
tar -tzf "$snapshot" >/dev/null
cp -n -- "$PKG" archive/source-packages/
tar -xzf "$PKG" -C .
echo "Verified backup: $snapshot"
make clean
make -j"$(nproc)"
make check check-config check-parser check-pcie check-audit check-maya-nvme check-modules check-settings-module check-ai-module check-cpu-module check-ui
printf '\nActive source: '; cat VERSION
printf 'Branch: '; git branch --show-current
printf '\nGit changes (not committed):\n'; git status --short
echo 'Step 2 installed; no Git push, tag, or system installation.'
