# v0.8.0-dev.3 — source boundaries and regression gates

This development-only checkpoint starts from `0.8.0-dev.2` and does not
replace the public `v0.7.0b` GitHub tag. The UI remains 840 × 660.

## Ownership

| Module | Mutable lifetime and limits | Calls into |
|---|---|---|
| `ai/http` | A reusable libcurl easy handle and per-request auth headers, bounded to the caller's response capacity | libcurl, loopback TCP |
| `ai/llama_process` | Cached process metadata with a 15-second refresh; API keys are never logged | `/proc`, `config/settings`, `ai/log_parser` |
| `ai/log_files` | Returns a caller-owned NUL-terminated buffer of at most 512 KiB, skipping partial leading lines | local filesystem, `config/settings` |
| `monitor/sensors` | Finds file descriptors and labels stored in the app's internal system telemetry state | Linux `/sys/class/hwmon`, `core/util` |
| `monitor/desktop_probe` | No persistent mutable state; reads bounded NUL-terminated process environments | `/proc`, package filesystem |

The main application still owns settings selection, telemetry snapshots,
log-event correlation, X11, GPU tuning, sysfs write authority and scheduling.
In particular, **the watchdog still shares a thread with potentially blocking
telemetry**, and no real-hardware timing guarantee is claimed.

## Test coverage

- All prior `make check-*` test targets continue to pass.
- `make check-sources-module` compiles and links against modules without
  including `amd-gpu-diagnostic.c`. It tests HTTP limit enforcement, llama
  argv parsing (including spaced paths), manual log source selection,
  log tails above 512 KiB, sensor read-only PWM max, and NUL-delimited
  desktop environment parsing including truncated records.
- Xvfb checks cover AI, GAMING, Tuning, Settings, Desktop and Events.
- Module-only tests validate algorithms and bounds, not live driver behavior.

## Next refactor checkpoint

Extract stateful desktop/session assembly and GPU resource readback before
attempting to isolate watchdog/tuning and standardize X11 UI primitives.
Firmware writes must retain a single authority, safe GPU switching and
existing ARM/APPLY semantics. Concurrency is a later measured change.
