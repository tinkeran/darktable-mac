# Phase 0 — Fork Setup & Baseline Audit

Tracking checklist for Phase 0 as defined in [PRD.md](PRD.md#9-mvp-phasing-recommended).

## Setup

- [x] Fork `darktable-org/darktable` → `tinkeran/darktable-mac` on GitHub
- [x] Clone locally to `~/Documents/darktable-mac`
- [x] Init submodules (LibRaw, OpenCL, libxcf, lua-scripts, rawspeed, whereami, tests/integration)
- [ ] Confirm local build succeeds on Apple Silicon (deps: cmake + Homebrew build stack; see `.github/workflows/ci.yml` macOS job for the reference dependency list)
- [x] Confirm CI already covers macOS (Apple Silicon) builds — see `.github/workflows/ci.yml` `macOS` job (`macos-15`, Xcode 26.3, arm64). No new workflow needed for Phase 0.

## Baseline audit — does it already work out of the box?

For each PRD section, check current darktable behavior and note gaps.

- [ ] **6.1 Import & Organization** — film rolls / collections grouped by import date; dedup on import; EXIF capture-date vs import-date filtering
- [ ] **6.2 Keep/delete triage + 5-star rating** — audit darktable's existing reject (`r` key) / star-rating flow specifically against the **permanent-delete-to-macOS-Trash** requirement. Confirm whether "purge"/remove-from-library currently unlinks files directly vs. leaves them on disk vs. does nothing to the file. This is the key gap expected per PRD §8a/§10.
- [ ] **6.3 Non-destructive editing engine** — confirm edit stack / history stack behavior matches expectations (it should, natively)
- [ ] **6.4 Crop & align** — confirm auto horizon/perspective straightening exists or needs a module
- [ ] **6.5 Histogram editing** — confirm tone curve, exposure/contrast/highlights/whites/shadows/blacks, white balance are all present (expected: yes, natively)
- [ ] **6.6 Shadow/highlight recovery** — confirm existing tone equalizer / shadows-highlights module covers this; check dehaze/clarity equivalent
- [ ] **6.10 Mass export to JPEG** — confirm batch export UI covers resolution, quality, color space, watermark, metadata stripping, filename pattern, async/background export with progress

## Not yet started (Phase 2/3 scope, not Phase 0)

- Sky/person segmentation (SAM 2 / AI object mask)
- LaMa content-aware fill (tracks upstream [darktable-org/darktable#15006](https://github.com/darktable-org/darktable/issues/15006))

## Notes

Log findings from each audit item here as they're completed, with darktable version/commit tested against.
