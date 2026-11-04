# v0.8.0-dev.13 — Provenance, shutdown and timing stability

Development-only checkpoint based on `0.8.0-dev.12`. The published `v0.7.0b`
release and its GPU-policy semantics remain unchanged.

## Confirmed problems and fixes

1. **ABA monitor-source race** — The old AI worker identified results by
   comparing settings only. If users chose A, B, then A before a slow A sample
   completed, its old result could look current. Each monitor request and
   response now includes a monotonically increasing generation; both generation
   and source fields must match before applying data. On a settings change the
   GUI immediately invalidates earlier readings and queues a fresh request.
2. **ABA compositor discovery race** — The discovery worker already used a
   generation on GPU switches, but not on compositor selection changes. Those
   now increment the same generation; card and compositor identity remain
   additional validation.
3. **Indefinite LM Studio CLI call** — The synchronous `popen("lms ps ...")`
   branch checked model availability but never produced actual tokens/sec.
   It could block read-only worker completion and `pthread_join` on exit.
   It has been removed; API-only or log-derived measurements remain separate.
4. **Watchdog percentile visibility** — An optional eight-bucket histogram
   tracks *positive deadline lateness* and reports approximate p50/p95/p99
   upper bounds in milliseconds. No dynamic memory or file I/O is added to
   the high-frequency path. Values of -1 indicate a >500ms bucket.

## Local timing measurements still needed

Run from the development directory, with matched intervals, once idle and once
under a sustained Bionic/Project Maya workload. **Do not enable experimental
GPU tuning just to obtain timing data.** Exit each run normally so the opt-in
profiler reports its counters to stderr:

```sh
sudo -E env AMD_GPU_DIAG_TIMING=1 ./amd-gpu-diagnostic 2> "$HOME/amd-idle.log"
sudo -E env AMD_GPU_DIAG_TIMING=1 ./amd-gpu-diagnostic 2> "$HOME/amd-ai-load.log"
scripts/compare-watchdog-logs.sh "$HOME/amd-idle.log" "$HOME/amd-ai-load.log"
```

For a dev.12-versus-dev.13 comparison use the same AMD GPU, power policy,
intervals and workload. Deadline-miss totals and skipped-period estimates
scale with run duration; maximum and bucket percentiles give additional context.
The timing report is *not* a hard real-time guarantee. UI rendering, DPM/power
sampling, tracefs pumping and fallback discovery can still delay the watchdog.

## Validation boundaries

The synthetic tests exercise A→B→A source provenance, compositor identity,
coalesced worker inputs, rejected writes from earlier GPU-control fixtures,
parser regressions, and X11 rendering. They do **not** validate real RX 7900 XTX
policy writes, multiple GPU switching, shutdown with an unresponsive third-party
API, suspend/resume, or production tracefs under heavy event load.
