# UV-K5 v1 firmware — release notes (Cursor-maintained)

## Download

- **`bg7nzl-k5v1-<short-sha>.bin`** (attached to this release) — **packed** firmware from `fw-pack.py` / `firmware.packed.bin`, ready for normal flashing tools.
- GitHub may also list default **Source code** archives; those are normal and separate from the firmware asset.

## What’s new since the previous auto-release

### Firmware & UX

- **FM radio tuning** — **short presses** on **UP** and **DOWN** now advance the FM frequency by one step (same as hold-to-repeat), instead of only responding after a long press. Release after a held repeat still does not add an extra step.

---

*This file is the **source of truth** for auto-release descriptions. Before each `commit` + `push` to `main`, update it in English (narrative summary, not a raw commit list). GitHub Actions reads it verbatim when creating the `auto-*` release.*
