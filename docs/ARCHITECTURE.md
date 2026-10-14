# Architecture

AMD GPU Diagnostic Tool v0.5.2 is a single-process C application with two intentionally separate responsibilities:

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
