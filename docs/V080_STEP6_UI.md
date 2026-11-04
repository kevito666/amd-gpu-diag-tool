# v0.8.0-dev.6 — Shared UI module boundary

This checkpoint changes source organization, **not** the visual design, user
configuration, watchdog policy, or GPU controls.

## Module ownership

| Module | Owns | Does not own |
|---|---|---|
| `ui/types.h` | View/WorkflowMode/Gui/Rect types and compact dimensions | GPU state, parsing |
| `ui/draw.c` | Xlib text, fills, outlines, bars, buttons, panel titles | sysfs or input dispatch |
| `ui/layout.c` | Geometry for drawing + clicks, scroll extent and clamping | X server calls, system state |
| `ui/window.c` | Display/window/GC/font resources; session X11 WM/compositor properties | GPU policy writes, app settings persistence |

The controller still owns the event loop, page render functions, application
state, and click-action dispatch. `ui/window` receives the compositor source
explicitly, and layout receives workflow mode explicitly rather than reading
a mutable global. No back-reference to the application translation unit.

## Testing and compatibility

`make check-ui-layout-module` is X-server-free and verifies tab/help spacing,
footer controls, Tuning tile steppers and actions, Settings rectangles,
AI log button, scroll extents and clamping, and resize-adjusted hitboxes.

`make check-ui` exercises AI, Gaming, Tuning, Settings, Desktop and Events on
Xvfb without GPU writes. Screenshot captures of AI, GAMING, Settings and Tuning
with the same 1280x900 virtual display were **pixel-identical** to dev.5.

The application uses the same 840x660 window, existing configuration schema,
existing power/tuning safety state, and 5 ms configured watchdog interval.
That interval still is *not* a hard real-time deadline.

## Next

Extract the independently renderable page bodies (AI/Gaming first, then
Tuning/Settings/desktop) using narrowly typed read-only snapshots. Prefer
preserving output before introducing layout improvements. Then review input
dispatch and scheduling. Never let a UI module perform GPU firmware writes.
