#!/usr/bin/env bash
set -Eeuo pipefail

APP_NAME="amd-gpu-diagnostic"
BIN="/usr/local/bin/$APP_NAME"
WRAPPER="$HOME/.local/bin/amd-gpu-diagnostic-gui"
DESKTOP="$HOME/.local/share/applications/amd-gpu-diagnostic.desktop"
SRC_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf '\n==> %s\n' "$*"; }

command -v pacman >/dev/null 2>&1 || die "This installer currently supports Arch Linux / pacman."
[[ -n "${DISPLAY:-}" ]] || die "DISPLAY is not set. Run this from your X11/XLibre desktop session."

info "Installing minimal dependencies"
sudo pacman -S --needed base-devel libx11

info "Building and checking AMD GPU Diagnostic Tool"
cd "$SRC_DIR"
make clean
make check
make

[[ -x "./$APP_NAME" ]] || die "Build completed but $APP_NAME was not created."

info "Installing binary"
sudo install -Dm755 "./$APP_NAME" "$BIN"

info "Installing X11/XLibre launcher"
mkdir -p "$(dirname "$WRAPPER")"
cat > "$WRAPPER" <<'EOF'
#!/usr/bin/env bash
set -e

: "${DISPLAY:?DISPLAY is not set. Launch from your X11/XLibre session.}"

args=(env "DISPLAY=$DISPLAY")

if [[ -n "${XAUTHORITY:-}" ]]; then
    args+=("XAUTHORITY=$XAUTHORITY")
elif [[ -f "$HOME/.Xauthority" ]]; then
    args+=("XAUTHORITY=$HOME/.Xauthority")
fi

exec sudo "${args[@]}" /usr/local/bin/amd-gpu-diagnostic "$@"
EOF
chmod 755 "$WRAPPER"

info "Installing desktop-menu entry"
mkdir -p "$(dirname "$DESKTOP")"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=AMD GPU Diagnostic Tool
Comment=AMDGPU diagnostics, telemetry, and power-policy watchdog
Exec=$WRAPPER
Terminal=false
Categories=System;Monitor;
StartupNotify=false
EOF

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi

printf '\nInstallation complete.\n'
printf 'Launch from the application menu or run:\n  %s\n' "$WRAPPER"
