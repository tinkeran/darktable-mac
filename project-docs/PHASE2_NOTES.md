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

## Update (2026-08-01): items 1 and 2 above fully verified with the real model; item 3 mechanically verified

Ran a real end-to-end spike using the actual published SAM 2.1 model, not synthetic data:

- **Real download**: fetched `mask-object-sam21-small.dtmodel` (169.6MB) from the real
  `darktable-org/darktable-ai` GitHub release (`release-5.6.0`). SHA-256 matched the published
  `versions.json` manifest exactly (`06a629ac...`) — the artifact darktable's own integrity check expects
  is exactly what's actually published; not a stub or placeholder.
- **Real extraction**: extracted with `bsdtar` (libarchive's own CLI — the same library darktable links)
  into `mask-object-sam21-small/{config.json, encoder.onnx (162.7MB), decoder.onnx (20.6MB)}`, matching the
  layout `ai_models.c` expects. `config.json` confirms genuine Meta SAM 2.1 (Hiera Small) weights,
  Apache-2.0 licensed, source `facebookresearch/sam2` — exactly the model PRD §8b specified.
- **Real registry discovery**: staged the extracted model at `$XDG_DATA_HOME/darktable/models/` and
  launched the actual built `darktable` binary with `--luacmd` running a small Lua script (`darktable.ai`
  Lua bindings, `src/lua/ai.c`) against a scratch config/library. `darktable.ai.models()` correctly reported
  `mask-object-sam21-small` as `status=2` (ready), vs `status=0` for models never downloaded — the registry
  scan genuinely recognizes a real staged model.
- **CoreML/ANE path — resolved, item 1 above**: `darktable.ai.load_model("mask-object-sam21-small",
  "coreml", "encoder.onnx")` and `...,"decoder.onnx")` both succeeded. With `-d ai` debug logging on, the
  log shows `attempting to enable Apple CoreML (V2)... Apple CoreML (V2) enabled successfully` for **both**
  the encoder and the decoder — no silent fallback to CPU. This is a real Apple Silicon Neural Engine
  execution path, not just a code read.
- **Real inference — resolves the mechanical risk in item 3 above**: called `ctx:run(...)` (the real
  `dt_ai_run` path) on the loaded encoder with a `1×3×1024×1024` input tensor (SAM2's standard image
  size) — it executed successfully and returned a correctly-shaped `1×32×256×256` feature-map output. This
  is genuine inference through the real Meta SAM 2.1 weights, via ONNX Runtime 1.28.0, via the CoreML
  execution provider, on real Apple Silicon hardware — the full mechanical pipeline (download → verify →
  extract → discover → load → CoreML-accelerate → execute) works end-to-end.
- Test environment (170MB of downloaded model weights, scratch config/library) fully cleaned up after
  verification — nothing left in the repo or system from this spike.

**What's still open after this**: semantic/UX-level correctness — actually clicking a subject in the
darkroom on a real photo and confirming the resulting *mask* looks right (not just that the encoder/decoder
graphs execute) still needs either a live GUI session or a new test that replicates `segmentation.c`'s
exact point-prompt pre/post-processing (image resize/normalize convention, point-coordinate encoding,
mask decode/threshold) — that logic lives in unchanged, already-shipped upstream code, not something this
fork wrote, so the risk here is materially lower than it was before this spike, but it's not literally
proven pixel-correct.

## Revised Phase 2 scope

Original PRD framing was "add SAM 2 segmentation" (net-new). Actual state: **the core segmentation
capability is already implemented upstream and now verified end-to-end with real weights and real CoreML
acceleration**, including mask-system integration. Phase 2 is not "build it" — it's:

1. ~~Verify items 1-3 above~~ — **done**, see spike above. Remaining: a live-GUI (or new scripted)
   pixel-correctness check of the actual mask output on a real photo — lower priority now that the
   mechanical pipeline is proven.
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
