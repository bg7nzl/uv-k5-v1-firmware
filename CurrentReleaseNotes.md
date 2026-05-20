# UV-K5 v1 firmware — release notes (Cursor-maintained)

## Download

- **`bg7nzl-k5v1-<short-sha>.bin`** (attached to this release) — **packed** firmware from `fw-pack.py` / `firmware.packed.bin`, ready for normal flashing tools.
- GitHub may also list default **Source code** archives; those are normal and separate from the firmware asset.

## What's new since the previous auto-release

### CAT control

- **CAT mode** — serial protocol on the programming UART (38400 baud), PC tools under `tools/cat_control/`, compatible with the reference `github-repo` frame format.
- **Simplified CAT UX** — removed the dedicated CAT LCD page; CAT operates directly on the current VFO with a **CAT** label on the status bar. K1-style power levels on the wire are mapped to K5 LOW/MID/HIGH in firmware.
- **Web UI** — browser control with COM-port selection, live status, and channel preset management (`tools/cat_control/webui/`). Battery voltage and RSSI/S-meter display fixes in the UI.

### Build & packaging

- **`build-packed.sh`** — one-step ELF build plus versioned packed `.bin` output.
- **`run_webui.sh`** — local venv setup and CAT Web UI launcher.

### RF / transmission

- **AM and USB TX** — improved modulation handling and BK4819 register configuration for AM and USB voice transmission.
- **DSB-SC not included** — an experimental DSB transmit path was tried and **reverted**; this build does not ship DSB-SC TX.

### Flash / UI (size)

- **Frequency display helper** — `UI_FormatFrequency()` replaces repeated `sprintf` MHz formatting across main, menu, scanner, digimode, aircopy, and spectrum screens (~**140 bytes** `.text` savings measured on the current toolchain).
- **Size analysis** — `make` emits `firmware.map`; optional `make size-report` writes section, symbol, and disassembly listings (gitignored artifacts).

---

*This file is the **source of truth** for auto-release descriptions. Before each `commit` + `push` to `main`, update it in English (narrative summary, not a raw commit list). GitHub Actions reads it verbatim when creating the `auto-*` release.*
