#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Child-shell entry: tar -xOzf PACKAGE ./scripts/promote-v080-step16.sh | bash -s -- PACKAGE
set -euo pipefail
PKG="${1:-}"
ROOT="${AMD_DIAG_ROOT:-$HOME/src/amd-gpu-diag-tool}"
[[ -f "$PKG" && -d "$ROOT/.git" ]] || { echo 'Usage: promote-v080-step16.sh PACKAGE (Git checkout required)' >&2; exit 2; }
[[ "$(tar -xOzf "$PKG" ./VERSION 2>/dev/null)" == '0.8.0-dev.16' ]] || { echo 'Wrong source package version' >&2; exit 2; }
# Refuse absolute or upward-traversing members and archive symlinks.
if tar -tzf "$PKG" | grep -Eq '(^/|(^|/)\.\.(/|$)|^\./(\.git|archive)(/|$))'; then
    echo 'Unsafe source archive path' >&2; exit 2
fi
if tar -tvzf "$PKG" | grep -Eq '^([lh])'; then
    echo 'Refusing archive containing symlinks/hardlinks' >&2; exit 2
fi
cd "$ROOT"
branch=$(git branch --show-current)
current=$(cat VERSION 2>/dev/null || true)
case "$branch" in
    main)
        [[ "$current" == '0.7.0b' && -z "$(git status --porcelain)" ]] || { echo 'Expected clean public v0.7.0b main; switch to refactor branch or commit your work' >&2; exit 2; }
        if git show-ref --verify --quiet refs/heads/refactor/v0.8.0; then
            echo 'Refactor branch already exists; switch to it explicitly first' >&2; exit 2
        fi
        git switch -c refactor/v0.8.0
        ;;
    refactor/v0.8.0)
        case "$current" in 0.8.0-dev.1|0.8.0-dev.2|0.8.0-dev.3|0.8.0-dev.4|0.8.0-dev.5|0.8.0-dev.6|0.8.0-dev.7|0.8.0-dev.8|0.8.0-dev.9|0.8.0-dev.10|0.8.0-dev.11|0.8.0-dev.12|0.8.0-dev.13|0.8.0-dev.14|0.8.0-dev.15|0.8.0-dev.16) ;; *) echo "Unexpected refactor source version: $current" >&2; exit 2;; esac
        ;;
    *) echo "Expected main or refactor/v0.8.0 branch, not $branch" >&2; exit 2;;
esac
mkdir -p archive/snapshots archive/source-packages
stamp=$(date +%Y%m%d-%H%M%S)
snapshot="archive/snapshots/pre-v080-dev16-$stamp.tar.gz"
tar --exclude='./archive' -czf "$snapshot" .
tar -tzf "$snapshot" >/dev/null
cp -n -- "$PKG" archive/source-packages/
echo "Verified snapshot: $snapshot"
tar -xzf "$PKG" -C .
make clean
make -j"$(nproc)"
make check check-config check-parser check-pcie check-audit check-maya-nvme check-modules check-settings-module check-ai-module check-cpu-module check-sources-module check-session-module check-readback-module check-control-module check-ui-layout-module check-ui-input-module check-dashboard-module check-pages-module check-events-module check-poll-timing-module check-monitor-worker-module check-discovery-worker-module check-clients-module check-worker-lifecycle check-ui
printf '\nActive source: '; cat VERSION
printf 'Branch: '; git branch --show-current
printf '\nUncommitted work:\n'; git status --short
echo 'Development checkpoint installed; nothing committed, pushed, tagged or installed system-wide.'
