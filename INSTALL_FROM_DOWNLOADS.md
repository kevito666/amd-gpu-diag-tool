# Install v0.8.0-dev.16 from Downloads (Arch / XLibre)

Save the complete ZIP and the corresponding installer to `~/Downloads`:

- `amd-gpu-diagnostic-v0.8.0-dev.16-ui-fixes-complete-source.zip`
- `install-amd-gpu-v0.8.0-dev.16-ui-fixes.sh`

Then run the standalone installer in a **child Bash process**, so errors will not close your terminal:

```sh
bash ~/Downloads/install-amd-gpu-v0.8.0-dev.16-ui-fixes.sh --system
```

Omit `--system` to update and test the development source without installing a system-wide binary. Add `--no-tests` only if you intend to skip the test suite. Python 3's standard library extracts the archive; no `unzip` executable is required.

The installer backs up the existing source to `archive/snapshots/`, preserves Git and `archive/`, checks the ZIP contents, builds, checks the executable version, and runs regression/UI tests before optional installation. It never commits, pushes, or tags Git. It expects the `refactor/v0.8.0` branch, or a clean published v0.7.0b main checkout.

**This is a development checkpoint, not the public v0.8.0b beta.** Firmware tuning and live multi-GPU switching should be validated on the actual hardware before release.
