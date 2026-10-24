#!/usr/bin/env bash
# Promote the reviewed flat source archive into the *existing* Git checkout.
# Does not erase .git, the archive, or manually install the executable.
set -euo pipefail

PACKAGE="${1:-}"
ROOT="${2:-$HOME/src/amd-gpu-diag-tool}"
if [[ -z "$PACKAGE" || ! -f "$PACKAGE" ]]; then
  echo "Usage: bash scripts/promote-flat-beta.sh /path/to/amd-gpu-diagnostic-v0.7.0b-maya-nvme-flat-source.tar.gz [project-root]" >&2
  exit 2
fi
PACKAGE="$(realpath -- "$PACKAGE")"
if [[ ! -d "$ROOT/.git" || ! -f "$ROOT/Makefile" ]]; then
  echo "Expected an existing amd-gpu-diag-tool Git repository: $ROOT" >&2
  exit 2
fi

# Validate the archive before touching any files.
MEMBERS="$(tar -tzf "$PACKAGE")"
if grep -Eq '(^/|(^|/)\.\.(/|$))' <<< "$MEMBERS"; then
  echo "Refusing unsafe archive entry" >&2
  exit 2
fi
if ! grep -Fxq 'src/amd-gpu-diagnostic.c' <<< "$MEMBERS" ||
   ! grep -Fxq 'VERSION' <<< "$MEMBERS"; then
  echo "This is not the expected flat source archive." >&2
  exit 2
fi

cd "$ROOT"
mkdir -p archive/snapshots archive/source-packages
stamp="$(date +%Y%m%d-%H%M%S)"
BACKUP="archive/snapshots/pre-stabilized-beta-$stamp.tar.gz"
echo "Backing up the current Git tree and uncommitted files to $BACKUP"
tar --exclude='./archive' -czf "$BACKUP" .
tar -tzf "$BACKUP" >/dev/null

DEST="archive/source-packages/$(basename "$PACKAGE")"
if [[ ! -e "$DEST" ]]; then
  cp -p -- "$PACKAGE" "$DEST"
fi

echo "Promoting flat v0.7.0b files into $ROOT"
tar -xzf "$PACKAGE" -C "$ROOT"
if [[ "$(tr -d '\r\n' < VERSION)" != '0.7.0b' ]]; then
  echo "Unexpected version after extraction; snapshot is available in $BACKUP" >&2
  exit 1
fi

# Force a rebuild: extracted source timestamps are not necessarily newer
# than the existing local binary, so a plain `make` could skip the update.
make -B
make check check-config check-parser check-pcie check-audit check-maya-nvme
if command -v xvfb-run >/dev/null 2>&1 && command -v xauth >/dev/null 2>&1; then
  make check-ui
else
  echo 'GUI smoke tests skipped (need xvfb-run and xauth).'
fi

printf '\nCurrent version: '; cat VERSION
printf '\nGit working tree (review before committing):\n'
git status --short
printf '\nBackup: %s\nPackage copy: %s\n' "$BACKUP" "$DEST"
echo 'Promotion finished. Nothing was committed, tagged, or installed system-wide.'
