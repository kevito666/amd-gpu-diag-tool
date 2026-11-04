# v0.8.0-dev.14 — lifecycle and file-input hardening

This is an unpublished `refactor/v0.8.0` development checkpoint. The public
`v0.7.0b` tag and `main` branch must stay unchanged.

## Confirmed bugs and fixes

1. **Worker-private AI history after A → B → A:** Previously the foreground
   rejected stale results using generation tokens, but the sampling callback
   retained its token-rate delta baseline, prompt-cache data and log history.
   `background_monitor_reset_generation` now invalidates those private AI
   fields before sampling each *new* generation. CPU history and open hwmon
   descriptors persist independently and are not reset.
2. **Blocking nonregular log paths:** The manual log selection can name a
   FIFO or other special file. A blocking `fopen` on a FIFO with no writer
   could strand the sampler during shutdown. The log-tail reader now uses
   `open(O_RDONLY | O_NONBLOCK | O_CLOEXEC)` and validates its actual opened
   inode with `fstat` before using `fdopen`. Nonregular files are rejected.
   Existing bounded 512 KiB reads of regular log files are preserved.
3. **Unexercised stop/restart paths:** A standalone, gate-controlled fixture
   tests cancellation of a queued request when shutdown begins while another
   sample is active, idle worker stop, repeated stop and restart for both
   sampling workers. No sysfs, network or X11 access is used.

## Validation commands

```sh
make clean
make -j
make check check-config check-parser check-pcie check-audit check-maya-nvme
make check-modules check-settings-module check-ai-module check-cpu-module
make check-sources-module check-session-module check-readback-module
make check-control-module check-ui-layout-module check-ui-input-module
make check-dashboard-module check-pages-module check-events-module
make check-poll-timing-module check-monitor-worker-module
make check-discovery-worker-module check-clients-module
make check-worker-lifecycle check-ui
```

## Still outstanding before v0.8.0b

- Real RX 7900 XTX latency measurements at idle, gaming load and sustained
  AI inference, comparing dev.10/dev.13 to this build.
- Live GPU-switching and tuning write-failure checks on supported equipment.
- Active blocking read-only worker calls must complete before `pthread_join`
  returns. The new log-input protection removes one hang source; it is not a
  general-purpose forced thread cancellation system.
- Watchdog behavior still depends on OS scheduling, GPU readback timing,
  X11 event processing and tracing. No hard-real-time guarantee.

## No intentional changes

No Xlib UI geometry, control write policy, tuning ARM/APPLY semantics, on-disk
settings schema, GPU selection logic, event logging or public Git tag changed.
