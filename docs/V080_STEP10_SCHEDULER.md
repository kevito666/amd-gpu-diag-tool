# v0.8.0-dev.10 — scheduler latency diagnostics and bounded input

This is an incremental development checkpoint, **not** the public v0.8.0b.
It measures lateness of the existing single-threaded 5 ms *configured*
watchdog interval. The event loop still performs synchronous AI network
requests and filesystem/process discovery; **5 ms is not guaranteed**.

## Profiling

Run from the build directory (appropriate GPU permissions are required):

```sh
sudo -E env AMD_GPU_DIAG_TIMING=1 ./amd-gpu-diagnostic \
    2> "$HOME/amd-gpu-dev10-timing.log"
```

Close the application normally to print the report to stderr. Collect one
idle sample, then another during Bionic/llama.cpp or Project Maya inference.
Compare max and average duration of these named operations:

- GPU clients, System + AI, Desktop session, GPU power, DPM monitoring
- tracefs pump, X11 events, GPU switch, X11 render

The watchdog report includes max and mean lateness **among late cycles**,
counts of lateness >= 1 ms, 5 ms, 50 ms, 500 ms, and estimated skipped
periods. It does **not** report p99 latency or measure complete real-time
responsiveness. A missed scheduled period is not an enforcement attempt.
Sampling overhead is opt-in. No logging is done per tick; a report is
emitted only on orderly shutdown. Sudden termination/power loss means no report.

## Input boundary

- `ui/input.c` maps X11 actions, scroll keys, and safe log-path editing.
- X11 events are dispatched in batches of at most 64, preventing an
  unbounded event flood from indefinitely delaying the next policy check.
- No setting format, UI layout, or firmware-write behavior was changed.

## Before introducing a separate watchdog worker

- Keep all GPU sysfs writes behind `gpu/writes.c` and one policy owner.
- Extract read-only AI/system polling into independently scheduled workers
  that publish immutable or synchronized telemetry snapshots.
- Check cancellation and GPU-switch lifecycle; never leave stale descriptors
  accessible after rebinding to another card.
- Measure actual deadline jitter and recovery over sustained AI/NVMe load.
- Test write-failure paths and explicitly disable tuning across GPU changes.
- Do not claim hardware-validation without running on target machine.
