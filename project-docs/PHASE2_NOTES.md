# Phase 2 — AI-Assisted Selective Editing: groundwork notes

Per [PRD.md](PRD.md#9-mvp-phasing-recommended) Phase 2. Before writing any SAM 2 integration code, we
researched what darktable's existing AI infrastructure (introduced in 5.6 per PRD §8a) actually contains.

## Headline finding: SAM 2.1 segmentation already exists upstream

darktable's `master` (as forked, commit history includes upstream PR **#21492 "Add SAM-style segmentation
architecture"**, confirmed present on `upstream/master` too — this is real, merged upstream work, not
something specific to this fork) already ships:

- **`src/common/ai/segmentation.c`** (SAM 2.1 Hiera Small + SegNext point-prompt segmentation) +
  **`src/develop/masks/object.c`** (UI integration as a new `DT_MASKS_OBJECT` drawn-mask type,
  `src/develop/masks.h:47`).
- **`data/ai_models.json`** lists `mask-object-sam21-small` ("mask sam2.1 hiera small") as the **default**
  segmentation model, plus `mask-object-segnext-b2hq` as an alternative, and `denoise-nind` /
  `rawdenoise-nind` / `upscale-realplksr` for the neural-restore module.
- A generic, reusable **AI backend** (`src/ai/backend.h`, `backend_common.c`, `backend_onnx.c`) — both
  segmentation and neural-restore call the same `dt_ai_load_model`/`dt_ai_run` API. **Not bespoke per
  feature.** A future SAM2-adjacent module (e.g. sky segmentation) would reuse this, not reinvent it.
- A model download pipeline (`src/common/ai_models.c`): SHA-256-verified ZIP (`.dtmodel`) downloads via
  libcurl, extracted via **libarchive** (path-traversal guarded) into `$XDG_DATA_HOME/darktable/models/`.
- **CoreML execution provider already wired for Apple Silicon** (`backend_onnx.c:1714-1752,2076-2103`),
  using `MLComputeUnits=ALL` so eligible ops route to the ANE automatically. The lightweight decoder is
  deliberately forced to CPU (overhead not worth it for a small graph); only the heavier encoder uses the
  configured provider.
- Segmentation output is **vectorized** (potrace-style tracing via `common/ras2vect.h`, optional DenseCRF
  edge refinement) into ordinary `DT_MASKS_PATH` shapes committed to `dev->forms` — meaning it plugs
  directly into darktable's **existing** mask-sharing mechanism (a `dt_masks_form_t` is a named,
  ID-referenced global object; any module's `blend_params.mask_id` can point at it, and the same shape can
  already be attached to multiple modules simultaneously). This is exactly the PRD §6.8 requirement
  ("sky adjustment + separate person adjustment stacked") — **no new mask-sharing infrastructure needed.**

Darktable's other raster-mask mechanism (`DEVELOP_MASK_RASTER`, `pixelpipe_hb.c:3518-3600`) is a
*different, module-to-module* thing (module A's live per-pixel output feeds module B downstream) — not
what SAM2 needs or uses; it exports as vector shapes into the shared forms list instead.

## Build verified: `-DUSE_AI=ON` works cleanly in this environment

- `./build.sh --enable-ai` (translates to `-DUSE_AI=ON`) builds clean. `darktable --version` reports
  `AI -> ENABLED`.
- Confirmed via `CMakeCache.txt` + `otool -L` that it resolved and linked against **Homebrew's onnxruntime
  1.28.0** (`/opt/homebrew/opt/onnxruntime`), not the CMake-default auto-download fallback (pinned to an
  older 1.24.4) — `FindONNXRuntime.cmake`'s system-package search found the Homebrew install because it's
  not keg-only. `libarchive` (keg-only) is already handled by fork-agnostic macOS-specific
  `brew --prefix libarchive` logic in `src/CMakeLists.txt:502-513`.
- `onnxruntime` and `cmocka` (needed for `BUILD_TESTING=ON`, see below) are **not yet in `.ci/Brewfile`**
  as of this writing for `-DUSE_AI=ON` builds specifically — `onnxruntime` already was in the Brewfile;
  `cmocka` is test-only and was installed ad hoc for this verification pass, not added to the Brewfile
  (only needed if `BUILD_TESTING=ON`, which isn't the default).

## Unit test fixed and passing: real ONNX inference confirmed, not just a compile check

`src/tests/unittests/ai/test_ai_backend.c` (cmocka-based) didn't link out of the box — its
`dt_stubs.c` stub file was stale relative to what `backend_common.c`/`backend_onnx.c` currently call
(`dt_conf_get_bool`, `dt_conf_get_int`, `dt_conf_key_exists`, `dt_control_log`,
`dt_loc_get_user_cache_dir` were all missing). Also found and fixed a stub bug where `dt_conf_get_string`
returned `"cpu"` for *every* config key, including `plugins/ai/ort_library_path`, causing the loader to
try to `dlopen("cpu.so")`. Fixed on branch `fix/ai-unittest-stubs` (merged) — **all 17 tests now pass**,
including `test_inference`, which is a genuine ONNX Runtime 1.28.0 load-and-run against the checked-in
`test-multiply` model (CPU provider). This confirms the shared AI backend that SAM2 segmentation depends
on actually executes correctly in this environment, not just compiles — a meaningful chunk of the
"riskiest unknown" flagged during research is now resolved by a real passing test, not just a code read.

This stub fix is a good candidate for an upstream PR per this fork's contribution policy
([FORK_README.md](FORK_README.md)) — it's a narrowly-scoped, generally-useful fix unrelated to any
fork-specific behavior.

## What's still unverified (narrower than before, but real)

1. **CoreML/ANE path specifically** — the passing test forced the CPU provider. The CoreML execution
   provider code path (`_try_coreml_v2`) has not been exercised in this session.
2. **Real SAM2 model download** — `mask-object-sam21-small`'s actual weights have not been downloaded from
   whatever repository `plugins/ai/repository` points to by default. Static code read only; the download
   pipeline itself (libcurl + libarchive + SHA-256 verify) is plausible but unconfirmed against a live
   remote model.
3. **End-to-end segmentation on a real photo** — clicking a subject in the darkroom and getting a real mask
   has not been done (would need either a live GUI session or a real RAW/JPEG test image plus a scripted
   equivalent of the `object.c` interactive flow — no darktable GUI automation tool is available in this
   environment).

## Revised Phase 2 scope

Original PRD framing was "add SAM 2 segmentation" (net-new). Actual state: **the core segmentation
capability is already implemented upstream**, including CoreML/ANE wiring and mask-system integration.
Phase 2 is not "build it" — it's:

1. Verify items 1-3 above (a live-GUI or scripted spike, ideally with a real photo).
2. Decide if `mask-object-sam21-small` accuracy/latency is good enough as-is, or if a different/newer SAM2
   checkpoint should be swapped in via the existing model-registry mechanism (`data/ai_models.json` +
   model repo) — likely no new C code either way.
3. Sky segmentation (PRD §8b: "darktable/Vision don't ship a dedicated sky class") — **still genuinely
   unsolved** and the one part of Phase 2 that may need real new work (fine-tune/find a sky model, or
   heuristic on top of the existing SAM2 point-prompt tooling). Not yet investigated this session.
4. UX: `DT_MASKS_OBJECT` is currently a toolbar icon inside the masks manager, not a headline "AI select
   subject" feature — worth a usability pass once the above is confirmed working.

Content-aware fill (PRD §6.9, Phase 3, LaMa) has **not** been investigated this session — worth the same
"check what upstream already has" pass before assuming it's net-new work, given how far off the Phase 2
assumption turned out to be.
