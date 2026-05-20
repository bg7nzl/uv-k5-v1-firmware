# UV-K5 v1 firmware — release notes (Cursor-maintained)

## Download

- **`bg7nzl-k5v1-<short-sha>.bin`** (attached to this release) — **packed** firmware from `fw-pack.py` / `firmware.packed.bin`, ready for normal flashing tools.
- GitHub may also list default **Source code** archives; those are normal and separate from the firmware asset.

## What's new since the previous auto-release

### CAT Web UI

- **Channel management** — preset repeater/channel library in the browser UI: load, apply, edit, and persist channel sets under `tools/cat_control/webui/data/`.
- **Startup script** — `tools/cat_control/run_webui.sh` creates a local venv, installs dependencies, and serves the UI (default `0.0.0.0:8765`).
- **Packaging helper** — `build-packed.sh` at the repo root builds the ELF and emits a versioned packed `.bin` in one step.

### RF / transmission

- **AM and USB TX paths** — improved modulation handling and BK4819 register setup for AM and USB voice transmission (including follow-up register tuning after initial AM work).
- **DSB-SC experiment removed** — experimental DSB transmit code was tried and **reverted**; this release does **not** ship DSB-SC TX.

### Flash / UI (size)

- **Frequency display helper** — consolidated MHz formatting into `UI_FormatFrequency()` across main, menu, scanner, digimode, aircopy, and spectrum screens, replacing repeated `sprintf` calls. Measured **~140 bytes** `.text` savings on the current build.
- **Build analysis** — `make` now emits `firmware.map`; optional `make size-report` produces section, symbol, and disassembly listings for size work (artifacts gitignored).

### Documentation

- **ADC audio / digimode planning** — new references for UART-pin (PA8) SARADC frequency measurement and a detailed **audio-ADC digimode** implementation plan.
- **DP32G030 vendor pack** — local copies of datasheet PDF, SVD, and fetch notes under `docs/external/dp32g030/` for interrupt/DMA audio sampling research.
- **Cross-link** — Vernier timing plan now points to the ADC audio measurement docs.

---

*This file is the **source of truth** for auto-release descriptions. Before each `commit` + `push` to `main`, update it in English (narrative summary, not a raw commit list). GitHub Actions reads it verbatim when creating the `auto-*` release.*
