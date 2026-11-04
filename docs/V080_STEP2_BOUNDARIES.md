# v0.8.0-dev.2 — Module boundaries and verification

This is a source-level checkpoint following `0.8.0-dev.1`; the `v0.7.0b`
GitHub release/tag remains unchanged.

| Module | Owns | Explicitly does **not** own |
|---|---|---|
| `config/settings` | Settings path, integer validation, INI serialization, secure atomic save | Runtime source selection, policy writes, UI state |
| `ai/log_parser` | Task-aware llama/Bionic timing records, RAM prompt-cache log interpretation | Logfile discovery, API networking, process enumeration, GPU/host memory measurement |
| `monitor/cpu` | Linux `/proc` process-stat parse, top-CPU delta ranking, core utilization | HWMON sensor discovery, CPU clock sysfs, GPU sysfs writes |

The adapters in `amd-gpu-diagnostic.c` remain short while the UI still uses
`SystemTelemetry`. We have intentionally *not* created a giant public shared
`AppContext` header or transferred source-owned globals between modules.

## Compatibility invariants

- The existing `~/.config/amd-gpu-diagnostic/settings.ini` keys and integers
  remain unchanged. File mode 0600, atomic rename and per-invoking-user privilege
  behavior have the same handling as the previous checkpoint.
- The original parser still keeps the latest 12 task records, merges by
  `(slot,task)`, and does not invent token throughput when not reported.
- `/proc/PID/stat` PID-reuse handling and 2-second top-process cadence remain
  unchanged. CPU core sampling continues on its existing 500 ms cycle.
- No modifications to device discovery, firmware writes, tuning, DPM watchdog,
  UI coordinates, screenshots, or asynchronous scheduling.

## Development gate

```sh
make clean
make -j"$(nproc)"
make check check-config check-parser check-pcie check-audit check-maya-nvme check-modules check-settings-module check-ai-module check-cpu-module check-ui
```

New module-only tests have no X11, libcurl, or GPU dependency and can be run
without an X server. `check-ui` still needs `xvfb-run` and `xauth`.

## Remaining architectural debt

- The fast GPU policy watchdog and synchronous AI HTTP requests still use
  the same event loop; its 5 ms interval is not a hard deadline guarantee.
- `SystemTelemetry` remains wide and couples AI, CPU sensors and UI rendering.
- Tests covering policy and UI still temporarily include the main app source.
- AI logfile discovery/tailing remains in the application translation unit.

Avoid changing these prematurely in Step 2: hardware validation and careful
lifetime/scheduling design are required in subsequent checkpoints.
