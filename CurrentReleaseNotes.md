# UV-K5 v1 firmware — release notes (Cursor-maintained)

## Download

- **`bg7nzl-k5v1-<short-sha>.bin`** (attached to this release) — **packed** firmware from `fw-pack.py` / `firmware.packed.bin`, ready for normal flashing tools.
- GitHub may also list default **Source code** archives; those are normal and separate from the firmware asset.

## What’s in this line of work

This snapshot reflects **recent development on `main` (about the last two days)** by **bg7nzl**, spanning both **radio features** and **build/CI infrastructure**.

### Firmware & UX

- **Broadcast FM (64–108 MHz)** — minimal wideband FM receive path with a **strict key policy** so FM mode stays controlled from the UI and build options.
- **DTMF** — new **`ENABLE_DTMF` master switch** so DTMF-related code paths can be gated cleanly at compile time; menu and behaviour wired through the new toggle.
- **CW & digital modes over UART** — large bring-up of **Digimode** (app + UI hooks, scheduler/radio integration), **Vernier** DSP helpers, documentation, and **PC-side tools** (Python helpers and GUI prototypes) for experimenting with digital modes from a host.
- **Digimode timing** — follow-up optimisation of **Vernier caching and transmit timing** for more stable digimode behaviour on the air.

### Build, Cursor rules, and GitHub Actions

- **Cursor workspace rules** — project-root conventions (paths, Docker vs bare `make`, and wording: **Docker = local**, **GitHub Actions = remote**).
- **CI pipeline** — firmware is built **inside Docker** (same idea as `compile-with-docker.sh` / `Dockerfile`) on `ubuntu-22.04`, then **packed** artifacts are published.
- **Artifacts & releases** — Actions upload a **single packed** artifact named by short SHA; **auto-tagged** releases (`auto-<sha>`) ship **`bg7nzl-k5v1-<sha>.bin`**. Manual **`v*`** tags still use the separate packed upload path for versioned releases.
- **Housekeeping** — experimental CI-only commits on `main` were **squashed** into one clean history entry; earlier **`auto-*` test releases** on GitHub were removed so this line of auto-releases can start from a single coherent description.

---

*This file is the **source of truth** for auto-release descriptions. Before each `commit` + `push` to `main`, update it in English (narrative summary, not a raw commit list). GitHub Actions reads it verbatim when creating the `auto-*` release.*
