# Dev.11 — Read-only monitor worker

## Thread ownership

- **Foreground thread:** 5 ms (configured, best effort) GPU policy loop, all
  AMDGPU sysfs writes, tuning ARM/APPLY, device switching, GUI/Xlib input and
  drawing, GPU power and client monitoring, DRM/session reads, tracefs.
- **Monitor worker:** CPU/per-core/process sampling, sensor FDs, NVMe device-wide
  counters, local AI API requests, llama process discovery and log parsing.
  It cannot touch Xlib or write GPU controls.
- **Communication:** `monitor_worker_request` copies current settings and GPU
  power into a single pending slot, replacing an older unstarted request.
  The callback performs I/O without holding the mutex. `monitor_worker_take`
  copies only fully published results under the lock. Both operations return
  quickly relative to I/O duration.
- Backend, parser, manual log name and selected NVMe identifiers are checked
  before publishing into the UI; changing settings clears the old display.
  All AI parsing state and CPU sensor handles are worker private. The main
  thread's copy of sensor FD *numbers* is never an owner and never closes them.

## Lifecycle and failure

- libcurl global initialization precedes worker startup; handle cleanup follows
  `monitor_worker_stop`/`pthread_join`, so no HTTP handle can outlive curl.
- The worker is asked to exit via a condition variable; any in-progress bounded
  HTTP or log operation completes before join. On shutdown, all discovered
  CPU hwmon descriptors are closed once. No thread is detached.
- If pthread creation fails, the app explicitly warns and performs sampling
  synchronously. This degraded fallback is functional but cannot prevent
  slow reads from affecting watchdog timing.
- A >5s-old finished monitor snapshot invalidates *token activity* and displays
  `Monitor delayed`; other historical sensor values remain last reported.
- System+AI profiler values are now background elapsed sampling durations, not
  CPU utilization or evidence of foreground blocking.

## Limits and beta tests

GPU `/proc` client scans, DRM/session detection, tracefs and X11 drawing remain
foreground operations. The OS is not running an RT scheduler and `5 ms` is a
requested iteration interval **not a hard realtime guarantee**. Validate
`AMD_GPU_DIAG_TIMING=1` idle vs. load on RX 7900 XTX; compare max lateness,
missed periods, client/session/trace/render costs with dev.10. Exercise settings
changes under inference, unplugged/missing NVMe, GPU switching while loaded,
normal shutdown and Ctrl-C. Do not claim hard latency improvement without host
measurement. Synthetic tests: `make check-monitor-worker-module` and
`make check-sources-module`, plus full `make check-ui`.
