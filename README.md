# AMD GPU Diagnostic Tool

A low-overhead Linux AMDGPU power-policy watchdog, live hardware monitor and
diagnostic correlator written in C.

It was built to investigate rapid AMD GPU power/clock-state transitions that can
cause audible coil whine, unstable power behaviour, or other Linux-only GPU
power-management problems while keeping the normal monitoring path lightweight.

> **Current release:** v0.4.1  
> **Platform:** Linux + `amdgpu`  
> **Default policy:** `profile_standard`  
> **Privilege:** root required

## Highlights

- Actively verifies and re-applies the selected AMDGPU power policy.
- Selectable power modes from the live terminal UI.
- Adaptive watchdog: low-overhead steady monitoring with a temporary fast burst
  after a real mismatch.
- Per-process GPU attribution using DRM `/proc/<pid>/fdinfo` accounting.
- Live temperature, fan, power, voltage, clocks, VRAM and utilization telemetry.
- VRAM usage bar on the main dashboard.
- GFXOFF monitoring and transition correlation.
- X11, X11Libre and Wayland session detection.
- Compositor detection including picom, KWin, Mutter, Xfwm4, gamescope and more.
- DRM connector/KMS display summary.
- In-memory event flight recorder and optional CSV logging.
- Optional isolated tracefs monitoring for AMDGPU, DRM scheduler and display events.
- Optional SCLK/MCLK DPM-state monitoring.
- Built-in sysfs polling benchmark.
- No GUI toolkit or ncurses dependency.

## Dashboard

The normal screen is intentionally a summary. Detailed information lives on
specialist pages.

```text
AMD GPU Diagnostic Tool v0.4.1
AMD Radeon RX 7900 XTX
card1  PCI 0000:03:00.0  ID 1002:744c  VRAM 24.0 GiB

Hardware: Temp 51 C | Fan 843 RPM (32%) | Power 81 W | VDDGFX 0.781 V
Clocks:   GFX 1427 MHz | MEM 1249 MHz | GPU 18% | MEM busy 7%
VRAM  [########....................]  6.84 / 24.0 GiB  28.5%

LOCK      profile_standard / locked
POWER     GFXOFF + utilization summary
CLIENTS   top GPU processes
DESKTOP   X11Libre / picom / display summary
EVENTS    latest correlated transition
TRACE     trace state + latest kernel actor
```

Values shown above are illustrative; the program displays the interfaces exposed
by the installed kernel and GPU.

## Build

Requirements:

- Linux
- GCC or a compatible C compiler
- Linux `amdgpu` driver
- `make`

```bash
make
sudo ./amd-gpu-diagnostic
```

Install system-wide:

```bash
sudo make install
sudo amd-gpu-diagnostic
```

Uninstall:

```bash
sudo make uninstall
```

## Power modes

The mode keys are always shown at the bottom of the UI:

```text
[1] Auto  [2] Low  [3] High  [4] Standard
[5] Min-GFX  [6] Min-MEM  [7] Peak  [8] Manual
```

Normal launch defaults to `profile_standard`:

```bash
sudo ./amd-gpu-diagnostic
```

Override it explicitly:

```bash
sudo ./amd-gpu-diagnostic --mode high
```

Supported policies:

- `auto`
- `low`
- `high`
- `profile_standard`
- `profile_min_sclk`
- `profile_min_mclk`
- `profile_peak`
- `manual`

## Views and hotkeys

| Key | Action |
|---|---|
| `0` | Dashboard |
| `P` | Per-process DRM GPU clients |
| `G` | Power / clocks / DPM / GFXOFF / PCIe |
| `W` | Desktop / compositor / connectors / KMS |
| `E` | Event flight recorder |
| `R` | Kernel trace status |
| `H` | Help |
| `1..8` | Select power mode |
| `S` / `M` | Select manual SCLK/MCLK DPM level |
| `X` | Clear manual clock locks |
| `Space` | Pause/resume enforcement |
| `+` / `-` | Adjust steady watchdog interval |
| `A` | Toggle adaptive correction burst |
| `C` | Toggle expensive DPM SCLK/MCLK telemetry |
| `D` | Cycle DPM telemetry rate |
| `B` | Policy-only sysfs benchmark |
| `L` | Toggle CSV event logging |
| `T` | Toggle isolated kernel trace |
| `Q` | Quit |

## Monitoring architecture

The program deliberately separates fast enforcement from slower diagnostic
sampling:

| Monitor | Default |
|---|---:|
| Policy watchdog | 5 ms / 200 Hz |
| Adaptive correction burst | 500 us / 2 kHz for 250 ms |
| Basic hardware telemetry | 10 Hz |
| Power profile / PCIe / gpu_metrics | ~1 Hz |
| Per-process DRM clients | 2 Hz |
| Desktop / KMS discovery | 0.2 Hz |
| `pp_dpm_sclk` / `pp_dpm_mclk` | Off by default; optional 4 Hz |
| Kernel tracefs | Off by default |

The process also defaults to `nice +10` so latency-sensitive applications such
as PipeWire get scheduling preference.

## Why DPM-state polling is optional

On the RX 7900 XTX development system, reading `pp_dpm_sclk` and `pp_dpm_mclk`
continuously was substantially more expensive than reading the power-policy
attribute. Very aggressive polling could also disturb latency-sensitive audio.

For that reason, the normal watchdog reads only the inexpensive policy interface.
Detailed DPM-state telemetry is opt-in.

## Per-process GPU attribution

The Clients page scans standardized DRM accounting records in
`/proc/<pid>/fdinfo` for the selected GPU and calculates activity deltas for
available engines such as:

- GFX
- compute
- DMA / SDMA / copy
- media / video
- VRAM residency or allocation

This makes it possible to correlate a clock or power event with processes such
as a game, browser GPU process, Telegram, picom, KWin, Xorg/Xwayland, gamescope
or Steam.

## Event flight recorder

The latest diagnostic events are kept in RAM. Events can include:

- policy reset / correction
- GFXOFF transition
- large sampled power step
- optional SCLK/MCLK DPM-state change

Each event records the current top DRM client and, when trace mode is enabled,
the latest kernel trace actor.

Press `L` to log events to:

```text
/tmp/amd-gpu-diagnostic-events-<pid>.csv
```

## Kernel trace mode

Press `T` to create an isolated tracefs instance and enable available AMDGPU,
DRM scheduler and selected DRM display tracepoints. The global tracing buffer is
not used.

Tracing is deliberately off by default because a busy 3D workload can generate
a large number of events.

## Safety notes

This program writes AMDGPU power-management sysfs controls and therefore must be
run as root. Unsupported controls are not guaranteed to exist on every AMD GPU
or kernel version.

Aggressive polling is intentionally limited. Use `--unsafe-fast` only when you
are deliberately testing the system and understand that extremely high sysfs
poll rates can increase kernel latency.

## Project status

This is an early diagnostic tool developed and tested primarily around an AMD
Radeon RX 7900 XTX on Linux. Hardware and kernel interfaces vary, so reports from
other AMDGPU generations are useful.

Bug reports should include:

- GPU model
- kernel version
- Mesa version
- X11/X11Libre/Wayland session
- desktop/compositor
- relevant terminal output
- whether DPM telemetry or trace mode was enabled

## License

MIT. See [LICENSE](LICENSE).
