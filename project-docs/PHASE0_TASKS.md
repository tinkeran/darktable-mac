# Phase 0 — Fork Setup & Baseline Audit

Tracking checklist for Phase 0 as defined in [PRD.md](PRD.md#9-mvp-phasing-recommended).

## Setup

- [x] Fork `darktable-org/darktable` → `tinkeran/darktable-mac` on GitHub
- [x] Clone locally to `~/Documents/darktable-mac`
- [x] Init submodules (LibRaw, OpenCL, libxcf, lua-scripts, rawspeed, whereami, tests/integration)
- [x] Confirm local build succeeds on Apple Silicon — built clean on first try via `./build.sh` using the official Homebrew dependency list (`.ci/Brewfile`, 46 packages) and `packaging/macosx/1_install_hb_dependencies.sh` as reference. `curl` and `libarchive` are keg-only on this Homebrew install, so `PKG_CONFIG_PATH`/`CMAKE_PREFIX_PATH` had to include `/opt/homebrew/opt/{curl,libarchive}` explicitly.
  - Result: `darktable 5.7.0+193~g8bc475111c [mac]` — OpenCL, LibRaw 0.22.0, Lua, gPhoto2 (tethering), OSMGpsMap, GMIC, GraphicsMagick, libavif/libheif/libjxl/OpenJPEG/OpenEXR/WebP all enabled. AI disabled (expected — off by default, `-DUSE_AI=ON` needed later for Phase 2/3).
  - Build is un-installed (default `build.sh` behavior); binary runs directly from `build/bin/darktable`.
- [x] Confirm CI already covers macOS (Apple Silicon) builds — see `.github/workflows/ci.yml` `macOS` job (`macos-15`, Xcode 26.3, arm64). No new workflow needed for Phase 0.

## Baseline audit — does it already work out of the box?

Audited by source inspection against commit `8bc475111c` (master). File:line references below.

- [x] **6.2a Delete-to-Trash — ALREADY SOLVED, better than the PRD assumed.** darktable has three distinct actions in `src/control/jobs/control_jobs.c`:
  - **Remove** (`dt_control_remove_images()`, `control_jobs.c:2088`) — DB row only, file untouched (dialog explicitly says "without deleting files on disk", `control_jobs.c:2106-2107`).
  - **Delete** (`dt_control_delete_images()`, `control_jobs.c:2118`) — physically removes via `delete_file_from_disk()` (`control_jobs.c:1138`), gated by a `send_to_trash` preference (`control_jobs.c:1145`; UI toggle "delete (trash)", `src/libs/image.c:304-309`). On macOS this calls `dt_osx_file_trash()` (`src/osx/osx.mm`) — i.e. **real Trash support already exists**, not a hard unlink. Falls back to a "delete permanently" prompt if trashing fails (`control_jobs.c:1054-1096`).
  - **Reject** (star=0 flag) never deletes/trashes by itself — purely a flag (`src/common/ratings.c`).
  - **Correction to PRD §8a/§10/§12**: the "permanent delete → Trash" behavior called out as the expected Phase 1 gap **does not need to be built** — just needs to be enabled by default / exposed clearly in this fork's default preferences. This significantly shrinks Phase 1 scope.
  - **Correction (2026-08-01): the RAW+JPEG-pair gap noted below was wrong — it's also already solved, no new code needed.** The first audit pass only grepped for literal "raw+jpeg" strings and missed darktable's generic **grouping** feature: on import, same-basename files in the same film roll are automatically assigned a shared `group_id`, with the RAW preferred as group representative (`src/common/image.c:1935-1974`, matched via `WHERE film_id = ?1 AND filename LIKE ?2 AND id = group_id`). When grouping display is enabled (`darktable.gui->grouping`), selecting a group representative auto-expands the selection to every group member (`src/common/selection.c:59-94`, `_selection_select()`), and the delete job's non-`only_visible` image-list query explicitly includes those hidden grouped siblings (`src/common/selection.c:501-536`, `dt_selection_get_list_query()`). So deleting a grouped RAW also deletes its paired JPEG (and vice versa) — for free, via existing, mature code.
  - **The only actual gap**: `ui_last/grouping` defaults to `false` upstream (`data/darktableconfig.xml.in`), so out of the box RAW+JPEG pairs display and act as independent images. **Fix applied in this fork** (branch `feature/raw-jpeg-pair-delete`): flipped the default to `true` in `data/darktableconfig.xml.in` — a one-line config default change, zero new C code, confirmed present in the rebuilt `build/share/darktable/darktableconfig.xml`. This is a general "always group" default (affects all grouped images app-wide: display, rating, export, delete — standard Lightroom-stack-like behavior), not delete-specific logic.
  - Not yet live-GUI-verified (would need an actual import of a same-basename RAW+JPEG pair through the GTK UI to confirm end-to-end) — source-level evidence is strong enough to treat this as resolved, but flag for a spot-check before relying on it for real deletes.
- [x] **6.2b 5-star rating — ALREADY SOLVED.** `src/common/ratings.c` implements 0–5 stars and reject as independent bits (`_ratings_apply_to_image()`, `ratings.c:58-80`; enum in `src/views/view.h:159-165`). Keyboard shortcuts 0–5 and `r` registered in `src/libs/tools/ratings.c:112-118`.
- [x] **6.1a Capture-date vs import-date — ALREADY SOLVED.** `dt_image_t` has independent `exif_datetime_taken` (`src/common/image.h:284`) and `import_timestamp` (`src/common/image.h:314`); both filterable as collection properties (`DT_COLLECTION_PROP_IMPORT_TIMESTAMP`, `src/common/collection.h:89`).
- [ ] **6.1b By-date folder organization on import — GAP.** Film rolls (`src/common/film.c`) are just the folder files already live in; there's no auto-copy into a `YYYY/MM/DD`-style tree on regular import (the date-pattern folder logic in `src/libs/import.c:1977-1998` is tethering-only). **Net-new work** if a Lightroom-style "By Date" physical folder layout is wanted, vs. just a virtual by-date collection view (which collections already give you for free via capture/import timestamp filtering).
- [ ] **6.1c Dedup on import — PARTIAL.** Import UI flags likely-already-imported files (checkmark column, `DT_IMPORT_UI_EXISTS`) via `dt_metadata_already_imported(basename, exif_datetime)` (`src/common/metadata.c:842`) — filename+capture-time match, not a checksum. It's advisory only (user must manually deselect, doesn't auto-skip). **Gap**: true checksum-based dedup + auto-skip is net-new work if required; current behavior may be "good enough."
- [x] **6.3 Non-destructive editing engine** — inherited/native to darktable (edit stack, history, XMP sidecars); not separately re-verified beyond what Phase 0 build already confirms compiles and runs.
- [x] **6.4 Crop & align — ALREADY SOLVED.** `src/iop/clipping.c` (crop) + `src/iop/ashift.c` (perspective/horizon) with genuine **automatic** line-structure detection (LSD line detection in `src/iop/ashift_lsd.c` + Nelder-Mead fit in `src/iop/ashift_nmsimplex.c`, entry point `do_fit()` at `ashift.c:5576`), not just manual angle entry. Auto-crop mode (`ASHIFT_CROP_LARGEST`) also exists.
- [x] **6.5 Histogram editing — ALREADY SOLVED.** `src/iop/exposure.c`, `src/iop/tonecurve.c`, `src/iop/temperature.c` (white balance) all present. Live RGB/luminance/waveform/vectorscope histogram widget in `src/libs/histogram.c`.
- [x] **6.6 Shadow/highlight recovery — ALREADY SOLVED.** `src/iop/shadhi.c`, `src/iop/toneequal.c`, `src/iop/hazeremoval.c` (dehaze) all present. Clarity/local-contrast equivalent: `src/iop/bilat.c` (local contrast) and `src/iop/highpass.c`.
- [x] **6.10 Mass export to JPEG — ALREADY SOLVED, comprehensive.** `src/libs/export.c` UI + async job (`_control_export_job_run`, `control_jobs.c:1830`, queued on `DT_JOB_QUEUE_USER_EXPORT`) with progress reporting. Confirmed configurable: max width/height (`export.c:375-376`), JPEG quality (piped to `jpeg_set_quality()`, `src/imageio/format/jpeg.c:52,204`), color space/ICC profile incl. sRGB/AdobeRGB (`export.c:1598-1600,2156`), watermark (as an IOP module, `src/iop/watermark.c`, bakeable via style/preset), GPS/metadata stripping (`src/libs/export_metadata.c:194-195,269,314`), filename pattern with variables (`dt_variables_expand()`, `src/imageio/storage/disk.c:386`).

## Phase 1 scope — revised based on this audit

Original PRD §9 assumed Phase 1's main net-new work was the trash-delete workflow. That's already built, and RAW+JPEG-pair delete turned out to be a one-line default flip (done — see above). Remaining Phase 1 candidates, in priority order:
1. ~~RAW+JPEG-pair-aware delete/trash~~ — **done**, `ui_last/grouping` default flipped to `true` on `feature/raw-jpeg-pair-delete`.
2. Decide whether by-date **virtual** collection view (already free via `import_timestamp`/capture-date filtering) is sufficient, or whether a physical by-date folder copy on import (6.1b) is actually required — recommend defaulting to virtual/collection-based and dropping the folder-copy requirement unless there's a specific reason to keep it.
3. Decide whether the existing filename+datetime advisory dedup (6.1c) is sufficient, or checksum-based auto-skip dedup is worth building.
4. ~~Set fork defaults: enable `send_to_trash` by default~~ — **already the upstream default** (`send_to_trash` and `ask_before_delete` both default `true` in `data/darktableconfig.xml.in`; confirmation dialog already shows a count and "using trash if possible" wording, matching PRD §6.2). Nothing to change.

**Net result: after this pass, items 1 and 4 of the originally-assumed Phase 1 work are both already solved by upstream** (1 via a one-line default flip, 4 via existing defaults). Only 2 (by-date folder layout) and 3 (dedup strength) remain open product decisions, not urgent engineering work — recommend revisiting those only if daily use of the app surfaces a real need, rather than building ahead of it.

## Not yet started (Phase 2/3 scope, not Phase 0/1)

- Sky/person segmentation (SAM 2 / AI object mask)
- LaMa content-aware fill (tracks upstream [darktable-org/darktable#15006](https://github.com/darktable-org/darktable/issues/15006))

## Notes

Audited against commit `8bc475111c` (fork's `master`, based on upstream `darktable-org/darktable` master as of 2026-08-01). Full audit performed via source-code inspection (Explore agent), not live GUI testing — GUI behaviors (dialogs, exact wording, preference locations) should be spot-checked by actually running `build/bin/darktable` before Phase 1 work starts.
