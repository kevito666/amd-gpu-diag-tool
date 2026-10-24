# Project Maya and AI NVMe cache monitor

## Scope

- Select `Project Maya (log)` as AI DATA SOURCE and AI LOG PARSER in Settings.
- Log discovery scans `~/src/project-maya`, `~/project-maya`, `~/Downloads/project-maya` for the newest readable `maya-*.log`; `Manual file` supports any absolute path.
- Maya's `glm stat:` decode timings, expert VRAM hit %, RAM fetches per token and SSD disk reads and waits per token are read from a bounded 512 KiB log tail every ~2 seconds; explicit `glm prefill:` timings are also parsed. Other Maya/DeepSeek engine lines display their original text.
- Log metrics are last observed completed summaries, not guaranteed to be live. If a field is missing, do not estimate it.
- Select an NVMe namespace with the new Settings button; the choice persists in `~/.config/amd-gpu-diagnostic/settings.ini` as `ai_nvme_device`.
- SSD rates derive from `/sys/block/DEVICE/stat`: fields #3 read sectors, #7 written sectors, #10 I/O busy milliseconds, with **512-byte sector units** per Linux block statistics ABI. Rates use `CLOCK_MONOTONIC` to obtain elapsed seconds and show MiB/s.
- Read/write counters include **all traffic on the selected NVMe namespace**, not only Project Maya, the selected process, nor actual SSD internal NAND/cache speed. They do not measure queue depth, latency histograms, or effective model tokens per byte.
- The SSD monitoring does not open the disk block device and cannot issue writes.

## Testing and limitations

- `make check-maya-nvme` tests the counter format, namespace validation, device changes and GLM parser using regression fixtures.
- `make check-config` includes round-trip persistence of `ai_nvme_device` and Project Maya mode.
- Synthetic X11 `make check-ui` covers Settings and the AI dashboard.
- Project Maya's logs change across engine versions and its DeepSeek-specific log grammar has not yet been validated against a real session. The UI preserves matching raw `ds4` lines and does not infer metrics until known.
- Real NVMe, multi-drive selection, real Maya GLM and DeepSeek logs, and the watchdog latency impact still require local hardware testing.
