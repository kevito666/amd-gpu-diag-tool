# v0.7.0b: second-pass deep audit

**Audit baseline:** `amd-gpu-diagnostic-v0.7.0b-stabilized-flat-source.tar.gz` (the previous packaged candidate).\
**Scope:** hardware-free review of correctness, performance, consistency, state transitions, UI regression coverage and privileged operations. No physical AMDGPU sysfs writes were attempted. Version intentionally remains **0.7.0b**.

## Verified changes in this pass

| Subsystem | Root cause | Change / verification |
|---|---|---|
| Policy selection | `set_mode` changed `Target.policy` and discarded clock locks *before* the sysfs write had succeeded | Commit the target/lock state only after a successful write; a failed write retains the previous watchdog target. Regression included. |
| Manual DPM selection | `cycle_manual_level` set the watchdog target to `manual` before both driver writes had succeeded | Commit manual mode only after both writes succeed; on partial failure attempt a best-effort restore of the prior policy. Manual sysfs writes are not atomic and need real-hardware testing. |
| Tuning apply/reset | A rejected policy write could leave watchdog bookkeeping ahead of the driver | Commit desired policy only after successful policy write; reset reports partial failure. Tuning write sequences remain potentially partially applied—this is **not** an atomic hardware transaction. |
| AI source switching | The shared token-rate struct reused `prev_decoded`, timestamps and heuristic history when users changed backend | Explicitly reset backend-specific counters and cached activity when selected source changes. |
| AI parser settings | Switching parser/manual log selection could wait up to 30 s for log rediscovery; disabling parsing left old cache occupancy/history visible | Invalidate discovery/update timers and cached events when the log selection changes; clear log state when disabled. |
| Local API HTTP | A caller with 4–8 KiB response buffer could download up to 1 MiB before rejecting response | Bound curl's accumulating buffer by the smaller of caller capacity - 1 and 1 MiB; oversized responses still fail rather than yielding truncated JSON. |
| Log viewer permissions | With a direct-root launch lacking `SUDO_USER`, `xdg-open` would run as root | Prevent root execution of the viewer; require a non-root invoking sudo user for privilege drop. The fork still only proves launch was attempted, not that a graphical viewer opened successfully. |
| CPU clocks | Every 500 ms `sample_system` swept per-core `scaling_cur_freq`/`cpuinfo_cur_freq` and `cpuinfo_max_freq`, creating many redundant sysfs opens | Sample CPU clocks at most once every 2 s; per-core load, RAM, and temperature retain their 500 ms cadence. |

## Highest-priority unresolved issue: watchdog scheduling

`main` is a **single-threaded** loop. Every cycle starts by enforcing the GPU policy, but that same thread also performs potentially blocking telemetry (`http_get_auth` with 500 ms timeout, `/proc` GPU-client and process scans, log-tail reads), X11 queries, and event logging. This means **configured watchdog interval is not a hard scheduling guarantee**, even if the fast-path sysfs policy read is efficient. Blocking time, missed deadlines and observed loop Hz should be measured on the real RX 7900 XTX while Bionic is active. The program already tracks loop rate and missed deadlines; neither fully characterizes the worst-case delay of each individual subsystem.

For a later beta, extract the policy watchdog into a dedicated bounded-latency worker, with explicit synchronization for GPU switching, policy changes, tuning writes and shutdown; OR implement genuinely non-blocking event-driven telemetry. Do not casually add an unsynchronized thread to root-owned sysfs controls. This remains a real architecture risk rather than a patch hidden behind passing UI tests.

## Other remaining limitations / possible improvements

- llama.cpp `/slots` parsing is substring-based and uses the **first matching** `n_decoded`/`is_processing` field. Parallel-slot requests and server restarts can yield unrepresentative token rates or counter resets; a proper bounded JSON parser with slot identities is preferable.
- AI log extraction scans the last 512 KiB every 2 s, so counters are **log-tail-local** and cache occupancy is the last logged state, not live sampled host RSS. Logs with repeated slot/task IDs from prior server lifetimes can still be correlated incorrectly.
- The GPU client scan walks `/proc/<pid>/fd` once per second. This is inherently variable-cost and can block the watchdog, especially on a busy workstation.
- Top CPU-process reporting examines at most 512 processes per sweep. This limits overhead, but means the displayed top three are drawn only from the inspected subset.
- CPU frequency reporting uses kernel cpufreq information, not an APERF/MPERF-calibrated effective frequency or a per-core clock trace.
- Settings save is user-owned and atomic, but startup still requires root for policy writes. Device permissions, sysfs availability, and active graphics session integration must be validated on the target desktop.
- Actual GPU switch, tuning apply/reset rollback, hot-unplug, compositor detection, XLibre metadata, VSync and VRR behavior require real-hardware/user-session tests. Existing `check-ui` validates basic rendering, **not** visual overlap at every possible window size or input hitbox parity.
- The roughly 6,000-line main C unit should eventually be split into modules, but a sweeping mechanical reorganization immediately before beta would introduce avoidable risk.

## Validation performed

The strict `make check`, `check-parser`, `check-config`, `check-pcie`, `check-audit` and six Xvfb `check-ui` page smoke checks passed separately in the audit container. Added audit tests cover failed mode writes, switching token backends, rejecting stale logging data when disabled, and caller-sized HTTP limits. AddressSanitizer + UBSan tests passed for audit, AI log, configuration and PCIe fixtures. These are hardware-free checks, not a claim of live GPU safety verification.

## Release recommendation

Publish **only as a clearly marked prerelease** after locally verifying policy/watchdog loop Hz and deadline misses with AI requests active, settings round trips under `sudo -E`, manual DPM failures, reset/apply behavior, and the two new v0.7.0b screenshots. The single-threaded watchdog scheduling issue is the most important item to resolve if low-latency enforcement is an advertised guarantee.
