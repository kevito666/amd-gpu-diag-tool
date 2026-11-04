#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Install AMD GPU Diagnostic Tool 0.8.0-dev.16 from a downloaded complete-source ZIP.
# Usage: bash ~/Downloads/install-amd-gpu-v0.8.0-dev.16.sh [--system] [--no-tests] [--zip /path/to/file.zip]
# This script runs in a child shell: failures never exit your interactive shell.
set -euo pipefail

VERSION='0.8.0-dev.16'
ZIP_NAME='amd-gpu-diagnostic-v0.8.0-dev.16-ui-fixes-complete-source.zip'
PKG="${HOME}/Downloads/${ZIP_NAME}"
ROOT="${AMD_DIAG_ROOT:-${HOME}/src/amd-gpu-diag-tool}"
SYSTEM_INSTALL=0
RUN_TESTS=1

usage() {
    cat <<'USAGE'
AMD GPU Diagnostic Tool v0.8.0-dev.16 source installer

Usage:
  bash ~/Downloads/install-amd-gpu-v0.8.0-dev.16-ui-fixes.sh [options]

Options:
  --system       Also install the tested executable to /usr/local/bin with sudo.
  --no-tests     Build without running regression tests (not recommended).
  --zip FILE     Read a ZIP from a nondefault path.
  --help         Show this help.

By default: back up the source, update ~/src/amd-gpu-diag-tool on
refactor/v0.8.0, build and test. Your Git history, archive/ and GitHub
v0.7.0b release are preserved. Nothing is committed or pushed.
You can override the project location with AMD_DIAG_ROOT=/path.
USAGE
}

while (($#)); do
    case "$1" in
        --system) SYSTEM_INSTALL=1; shift ;;
        --no-tests) RUN_TESTS=0; shift ;;
        --zip) (($# >= 2)) || { echo 'Missing argument for --zip' >&2; exit 2; }; PKG="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for app in bash python3 tar make git sha256sum; do
    command -v "$app" >/dev/null 2>&1 || {
        echo "Missing required command: $app" >&2
        exit 2
    }
done

[[ -f "$PKG" ]] || { echo "ZIP not found: $PKG" >&2; exit 2; }
[[ -d "$ROOT/.git" ]] || {
    echo "Expected existing Git project at: $ROOT" >&2
    echo 'Set AMD_DIAG_ROOT=/path/to/checkout if yours differs.' >&2
    exit 2
}

STAGE="$(mktemp -d)"
trap 'rm -rf -- "$STAGE"' EXIT

# zipfile is part of Python's standard library. Strict extraction checks are
# performed before modifying the project directory. No unzip dependency.
python3 - "$PKG" "$STAGE" "$VERSION" <<'PY'
from pathlib import Path, PurePosixPath
from zipfile import ZipFile, BadZipFile
import os, stat, sys
zip_path, stage, version = sys.argv[1:]
stage = Path(stage)
prefix = f"amd-gpu-diagnostic-v{version}/"
seen = set()
bytes_total = 0
try:
    with ZipFile(zip_path) as z:
        entries = z.infolist()
        if len(entries) > 400:
            raise ValueError('unexpectedly many ZIP members')
        for info in entries:
            name = info.filename
            if not name.startswith(prefix) or '\\' in name or '\x00' in name:
                raise ValueError(f'unsafe or unexpected ZIP member: {name!r}')
            rel = name[len(prefix):]
            if not rel:
                continue
            p = PurePosixPath(rel)
            if p.is_absolute() or '..' in p.parts or p.parts[0] in ('.git', 'archive'):
                raise ValueError(f'unsafe ZIP member: {name!r}')
            if rel in seen:
                raise ValueError(f'duplicate ZIP member: {name!r}')
            seen.add(rel)
            ftype = stat.S_IFMT(info.external_attr >> 16)
            if ftype not in (0, stat.S_IFREG, stat.S_IFDIR):
                raise ValueError(f'symlink or special file refused: {name!r}')
            bytes_total += info.file_size
            if info.file_size > 16 * 1024 * 1024 or bytes_total > 64 * 1024 * 1024:
                raise ValueError('unexpectedly large archive contents')
        if z.testzip() is not None:
            raise ValueError('ZIP integrity verification failed')
        for info in entries:
            rel = info.filename[len(prefix):]
            if not rel:
                continue
            dst = stage.joinpath(*PurePosixPath(rel).parts)
            if info.is_dir():
                dst.mkdir(parents=True, exist_ok=True)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as src, dst.open('wb') as out:
                    while chunk := src.read(65536):
                        out.write(chunk)
                mode = (info.external_attr >> 16) & 0o777
                if mode & 0o111:
                    dst.chmod(0o755)
    expected = stage / 'VERSION'
    if not expected.is_file() or expected.read_text().strip() != version:
        raise ValueError('ZIP version does not match requested build')
    for required in ('Makefile', 'LICENSE', 'README.md', 'src/amd-gpu-diagnostic.c',
                     'scripts/promote-v080-step16.sh', 'tests/test_worker_lifecycle.c'):
        if not (stage / required).is_file():
            raise ValueError(f'required source file missing: {required}')
except (BadZipFile, OSError, ValueError) as exc:
    raise SystemExit(f'ZIP validation failed: {exc}')
print(f'ZIP checked: {len(entries)} members; {bytes_total} bytes uncompressed')
PY

cd "$ROOT"
BRANCH="$(git branch --show-current)"
CURRENT="$(cat VERSION 2>/dev/null || true)"
case "$BRANCH" in
    refactor/v0.8.0)
        case "$CURRENT" in
            0.8.0-dev.[1-9]|0.8.0-dev.1[0-6]) : ;;
            *) echo "Unexpected current version on refactor branch: $CURRENT" >&2; exit 2 ;;
        esac
        ;;
    main)
        if [[ "$CURRENT" != '0.7.0b' || -n "$(git status --porcelain)" ]]; then
            echo 'Main must be a clean v0.7.0b checkout. Switch to refactor/v0.8.0 first.' >&2
            exit 2
        fi
        if git show-ref --verify --quiet refs/heads/refactor/v0.8.0; then
            echo 'Refactor branch already exists. Switch to it before installing.' >&2
            exit 2
        fi
        ;;
    *) echo "Unsupported branch '$BRANCH'; switch to refactor/v0.8.0 first." >&2; exit 2 ;;
esac

mkdir -p archive/snapshots archive/source-packages
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="archive/snapshots/pre-v080-dev16-zip-${STAMP}.tar.gz"
echo "Backing up existing source to $ROOT/$BACKUP"
tar --exclude='./archive' -czf "$BACKUP" .
tar -tzf "$BACKUP" >/dev/null
[[ -s "$BACKUP" ]] || { echo 'Backup verification failed' >&2; exit 3; }

if [[ "$BRANCH" == main ]]; then
    git switch -c refactor/v0.8.0
fi

cp -n -- "$PKG" archive/source-packages/
echo "Updating source tree at: $ROOT"
cp -a -- "$STAGE/." "$ROOT/"

echo "Building $VERSION ..."
make clean
make -j"$(nproc)"
[[ "$(./amd-gpu-diagnostic --version)" == "$VERSION" ]] || {
    echo 'Version verification failed; source backup preserved.' >&2
    exit 3
}

if ((RUN_TESTS)); then
    if ! command -v xvfb-run >/dev/null; then
        echo 'Missing xvfb-run. On XLibre/Arch, install xlibre-xserver-xvfb before running all checks.' >&2
        echo "Backup available: $ROOT/$BACKUP" >&2
        exit 3
    fi
    echo 'Running all application and module regression tests...'
    make check check-config check-parser check-pcie check-audit check-maya-nvme \
         check-modules check-settings-module check-ai-module check-cpu-module \
         check-sources-module check-session-module check-readback-module \
         check-control-module check-ui-layout-module check-ui-input-module \
         check-dashboard-module check-pages-module check-events-module \
         check-poll-timing-module check-monitor-worker-module \
         check-discovery-worker-module check-clients-module \
         check-worker-lifecycle check-ui
fi

if ((SYSTEM_INSTALL)); then
    echo 'Installing validated binary system-wide (sudo make install)...'
    sudo make install
fi

printf '\n=== INSTALLATION COMPLETE ===\n'
printf 'Active version: '; cat VERSION
printf 'Git branch: '; git branch --show-current
printf 'Source: %s\n' "$ROOT"
printf 'Source backup: %s\n' "$ROOT/$BACKUP"
if ((SYSTEM_INSTALL)); then
    printf 'Binary: /usr/local/bin/amd-gpu-diagnostic\n'
else
    printf 'Built executable: %s/amd-gpu-diagnostic\n' "$ROOT"
    printf 'To install system-wide later: cd %q && sudo make install\n' "$ROOT"
fi
printf 'Git changes (not committed):\n'
git status --short
