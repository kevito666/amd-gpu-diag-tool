# Architecture

AMD GPU Performance and Diagnostic Tool v0.6.0 is a single-process C application with two intentionally separate responsibilities:

1. A low-overhead AMDGPU diagnostic/watchdog core using Linux kernel interfaces directly.
2. A minimal event-driven Xlib frontend for X11/XLibre.

## Dependency rule

The project avoids external libraries unless they materially improve correctness or provide functionality that is unreasonable to implement directly.

The GUI adds only Xlib. The diagnostic core continues to use libc plus kernel virtual filesystems/interfaces.

## Timing tiers

The 5 ms watchdog touches only the cheap power-policy attribute during stable operation. Expensive DPM reads, process accounting, desktop discovery, and tracing run independently at much lower rates or only when explicitly enabled.

System CPU/RAM sampling uses `/proc/stat` and `/proc/meminfo` at 2 Hz and does not add a library dependency.

The Xlib UI has no animation loop. X events are processed from the same main loop and the window is repainted only when data changed, with a maximum redraw rate of 4 Hz.

## X11 introspection

The GUI uses standard X11/EWMH mechanisms instead of shelling out to tools:

- X server vendor: `ServerVendor()`
- compositor: `_NET_WM_CM_S0`
- compositor PID: `_NET_WM_PID`
- window manager: `_NET_SUPPORTING_WM_CHECK` and `_NET_WM_NAME`

This is both lighter and more authoritative than launching `xprop`, `wmctrl`, or process-grep helpers.

## Tuning architecture

Tuning is intentionally split into three layers:

1. **Runtime authority** — the AMDGPU driver's `pp_od_clk_voltage` `OD_RANGE` data and hwmon `power1_cap_min` / `power1_cap_max` values are the hard bounds.
2. **Captured stock state** — values observed when the program starts are used as the local baseline for the green/amber/red aggressiveness display and for restoring the initial power cap.
3. **Reference database** — `src/gpu_specs.c` supplies AMD reference-card game/boost clocks, TBP, VRAM, memory data rate, bus width, and architecture. It is context only and is never used to widen a driver-exposed tuning range.

The tuning UI stages edits in memory. No clock, voltage, or power write occurs until the user explicitly arms writes and then applies. The arm state is cleared after every apply attempt.

The parser distinguishes modern Vega20+/RDNA min/max overdrive layouts, RDNA4 SCLK-offset layouts, and legacy multi-state tables. Legacy tables that cannot be represented without ambiguity are left read-only instead of being guessed.

## Thermal sampling

AMDGPU hwmon can expose up to three GPU temperature channels. The program reads each available `temp[1-3]_input`, label, critical limit, and emergency/shutdown limit. CPU package temperature is discovered independently from `k10temp`/compatible hwmon data. These sensors are displayed in the persistent top hardware strip.
