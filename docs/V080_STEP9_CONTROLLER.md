# 0.8.0-dev.9 — controller and diagnostic boundary

## Scope and invariants

The source is modularized without changing GPU firmware writes, tuning
ARM/APPLY/RESET, the selected sysfs card, persistent settings, or the current
single-threaded watchdog. All UI coordinates still come from ui/layout.

## Ownership

- `ui/chrome.[ch]`: fixed header, mode buttons, thermal bars, tabs and footer.
  The controller supplies `WorkflowMode`; this rendering code never writes a
  GPU policy. It uses the same geometry functions as click detection.
- `diagnostics/events.[ch]`: event ring, latest event, CSV lifecycle.
  Top-actor attribution is taken from the provided client snapshot.
- `diagnostics/trace.[ch]`: tracefs discovery, per-process trace instance,
  enable/disable and pipe pump. The caller owns the resulting TraceState.
- `app/poll_timing.[ch]`: optional, saturating counters for collector wall
  time in ns, monotonic timestamps and shutdown-only human-readable report.

## Profiling command

```sh
AMD_GPU_DIAG_TIMING=1 ./amd-gpu-diagnostic
```

Exit normally to receive a report on stderr. Logged fields include total
wall time, number of calls, average and maximum call time (ms), plus the
existing total watchdog deadline misses. The latter are **not** assigned to
any specific poller, and a call with high duration does not prove causation.
Collectors remain synchronous and can still delay the watchdog.

## Tests

`make check-events-module` verifies event attribution and ring wrap, without
GPU access or an X display. `make check-poll-timing-module` verifies timing
counters, saturation, formatting and the monotonic clock. Legacy tests
continue to cover parser/config/PCIe/tuning behavior and X11 renders.

## Future work

1. Extract X11 input dispatch from the controller without altering event
   behavior or edit/shortcut precedence.
2. Capture deadline lateness distributions and a measurable before/after
   performance baseline, not merely average watchdog Hz.
3. Only then evaluate a dedicated nonblocking monitoring worker while
   keeping one authenticated GPU-policy writer.
