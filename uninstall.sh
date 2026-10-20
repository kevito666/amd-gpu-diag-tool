#!/usr/bin/env bash
set -Eeuo pipefail

sudo rm -f /usr/local/bin/amd-gpu-diagnostic
rm -f "$HOME/.local/bin/amd-gpu-diagnostic-gui"
rm -f "$HOME/.local/share/applications/amd-gpu-diagnostic.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi

printf 'AMD GPU Performance and Diagnostic Tool removed.\n'
