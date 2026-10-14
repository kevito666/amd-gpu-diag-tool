# AMD GPU Diagnostic Tool

A lightweight X11/XLibre GUI for AMDGPU power-policy locking and diagnostics on Linux.

The application is written in C and uses plain Xlib for the interface. It intentionally avoids GTK, Qt, Electron, ncurses, libdrm, XCB as a direct dependency, and other GUI frameworks. The diagnostic core talks directly to Linux sysfs, procfs, debugfs, tracefs, and DRM fdinfo interfaces.

**Version:** 0.5.2  
**License:** GPL-3.0-or-later

## Designed for

This GUI build is intentionally X11/XLibre-first and is a particularly good fit for Arch Linux + XFCE. XLibre is detected from the running X server vendor string when available. The active X11 compositor is identified through the standard `_NET_WM_CM_S0` selection owner, which makes picom detection substantially more reliable than process-name guessing.

Wayland is not a target of this GUI build.

## Features

- AMDGPU power-policy watchdog with `profile_standard` as the default target.
- Adaptive correction: low-overhead steady polling with a short faster burst after an actual policy reset.
- Prominent top-of-window power-mode selector with clickable modes and keyboard shortcuts.
- A dominant full-width board-power bar plus always-visible bars for GPU load, VRAM, CPU load, system RAM, GPU voltage, and current.
- Live temperature, fan RPM/percentage, board power, GFX clock, and memory clock.
- Uses a real hwmon current sensor when available; otherwise shows an explicitly labelled 12 V board-current estimate from `board_power / 12 V`.
- Per-process DRM activity attribution.
- GFXOFF monitoring.
- Power-profile, PCIe, KMS, connector, plane, and display diagnostics.
- Exact X11 compositor-owner detection plus window-manager identification.
- Diagnostic event flight recorder and optional CSV logging.
- Optional isolated tracefs diagnostics.
- Manual GFX/MEM DPM-level cycling.
- Event-driven Xlib GUI with no animation/render loop.

## Runtime footprint

The GUI directly links only to:

- `libX11`
- `libc`

The current optimized binary is roughly 80 KiB on the development build system. Runtime size varies slightly by compiler/toolchain.

## Arch Linux requirements

On XFCE/XLibre you will almost certainly already have `libx11` installed.

```bash
sudo pacman -S --needed base-devel libx11
```

No GTK or Qt packages are required.

## Build

```bash
make
make check
```

## Run

AMDGPU power-policy controls normally require root. Preserve the X11 display/authentication environment when using sudo:

```bash
sudo -E ./amd-gpu-diagnostic
```

If your sudo configuration does not preserve X11 authentication:

```bash
sudo env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" ./amd-gpu-diagnostic
```

The application deliberately lowers its process priority by default so audio and game workloads receive scheduling preference.

## Main GUI

The top control/status block keeps power control and physical system state visible on every page:

- full GPU name
- DRM card / PCI address / PCI ID
- total VRAM
- requested vs actual AMDGPU policy and the eight power-mode buttons
- temperature, fan RPM/percentage, and board power
- GFX and memory clocks
- GPU-load and VRAM-usage bars
- CPU-load and system-RAM bars
- measured GPU voltage bar
- current bar: hardware current sensor when available, otherwise a labelled `P/12 V` board-current estimate

The Home view summarizes one or two important facts from every detailed page rather than duplicating all diagnostic data.

## Power modes

The power-mode buttons are always visible near the top of the window, immediately below the target/actual power-policy status:

- `1` Auto
- `2` Low
- `3` High
- `4` Standard (`profile_standard`)
- `5` Min-GFX
- `6` Min-MEM
- `7` Peak
- `8` Manual

`profile_standard` is the default.

## Views and controls

- `0` Home
- `P` GPU clients
- `G` Power / DPM
- `W` X11 / desktop / display
- `E` diagnostic events
- `R` kernel trace
- `H` help
- `I` About
- `Space` pause/resume watchdog
- `A` adaptive correction burst
- `C` DPM clock telemetry
- `+` / `-` watchdog rate
- `D` DPM telemetry rate
- `S` cycle manual GFX DPM level
- `M` cycle manual memory DPM level
- `X` clear manual clock locks
- `T` tracefs on/off
- `L` CSV logging on/off
- `Q` quit

The main tabs, power modes, watchdog, adaptive mode, DPM telemetry, trace, and CSV log toggles are clickable.

## Sampling design

The program deliberately separates inexpensive enforcement from expensive telemetry:

- Power-policy watchdog: 5 ms / 200 Hz by default.
- Adaptive correction burst: 500 us / 2 kHz for 250 ms after a real mismatch.
- Basic GPU hardware telemetry: 10 Hz.
- CPU/RAM telemetry: 2 Hz.
- Per-process DRM accounting: 2 Hz.
- Desktop/display discovery: 0.2 Hz.
- Expensive `pp_dpm_*` sampling: off by default.
- Kernel tracing: off by default.
- GUI redraw: at most 4 Hz and only when dirty.

This design exists partly because very aggressive amdgpu sysfs polling can cause scheduling latency and audio xruns even when average CPU utilization appears modest.

## XLibre / picom diagnostics

The Desktop page uses Xlib to query the live server directly:

- `ServerVendor()` identifies XLibre when the running server reports it.
- `_NET_WM_CM_S0` identifies the active compositor selection owner.
- `_NET_WM_PID` maps that owner back to its process (for example picom).
- `_NET_SUPPORTING_WM_CHECK` identifies the window manager (for example Xfwm4).

Process scanning remains as a fallback for diagnostic context.

## Security

This program writes privileged AMDGPU sysfs controls and currently runs the GUI process as root. X11 is not a privilege-separated display protocol, so this version is intended for a trusted, local, single-user desktop session. Do not expose the X server to untrusted clients.

A future split privileged helper / unprivileged GUI is possible, but it would add IPC and lifecycle complexity. This release favors the smallest practical implementation for a trusted workstation.

## License

GNU General Public License version 3 or later (`GPL-3.0-or-later`). See `LICENSE`.


## About

**Developer:** Kevin  
**Copyright:** © 2026 Kevin Kolasa  
**Developed in:** Akita City, Akita Prefecture, Japan

AMD GPU Diagnostic Tool grew from the practical need to understand Linux AMDGPU power-state switching, desktop/game rendering interactions, and associated coil-whine behavior.

The project prioritizes low overhead, simple native C, transparent Linux kernel interfaces, and minimal dependencies. Project direction, hardware testing, and technical decisions remain under direct developer review. AI-assisted development tools are used to accelerate implementation, testing, review, and documentation.

**License:** GPL-3.0-or-later
