# UV-K5 v1 firmware — release notes (Cursor-maintained)

## Download

- **`bg7nzl-k5v1-<short-sha>.bin`** (attached to this release) — **packed** firmware from `fw-pack.py` / `firmware.packed.bin`, ready for normal flashing tools.
- GitHub may also list default **Source code** archives; those are normal and separate from the firmware asset.

## What’s new since the previous auto-release

This update is mainly focused on making **Digimode exit cleanly without a reboot** and tightening the matching workflow guidance around local vs remote builds.

### Firmware & UX

- **Digimode exit control** — a **short press of `EXIT`** on the Digimode screen now leaves Digimode and returns the radio to normal operation instead of requiring a reboot.
- **Safer Digimode entry** — entering Digimode now actively stops conflicting background activity such as scanning, dual watch, and NOAA auto-scanning so the digital-mode state does not inherit unstable runtime context.
- **Safer Digimode teardown** — exit logic now clears Digimode runtime scheduling/TX state and reloads the normal VFO/radio configuration before returning to the main screen.

### Workflow notes

- **Push discipline** — project guidance now keeps commit-only actions local and defers release-note refresh/push-triggered CI behaviour until an explicit `push`.
- **Build wording** — plain “build/compile” requests are documented as meaning the local Docker build path unless GitHub Actions is explicitly requested.

---

*This file is the **source of truth** for auto-release descriptions. Before each `commit` + `push` to `main`, update it in English (narrative summary, not a raw commit list). GitHub Actions reads it verbatim when creating the `auto-*` release.*
