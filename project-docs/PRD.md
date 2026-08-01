# PRD: [Working Title] — Open Source Photo Editor for macOS (Fork of darktable)

**Author:** Raju
**Date:** August 1, 2026
**Status:** Draft v3 (all open questions resolved)

## 1. Overview & Vision

A fully open source macOS application for organizing and non-destructively editing personal photos, inspired by Adobe Lightroom. Rather than building from scratch, this project **forks an existing open source RAW editor (darktable — see Section 8a) and extends it**, so the work can be released publicly, contributed back upstream, and benefit from an existing mature codebase, community, and camera-support matrix.

Built for personal use first, but developed in the open (public GitHub fork) so features can be upstreamed where generally useful. Focus: import photos from a camera or card, organize by date, apply RAW-aware non-destructive edits (including AI-assisted selective adjustments), and mass-export finished JPEGs for sharing.

The defining difference from stock darktable is filling two gaps it doesn't fully solve today: automatic people/sky/background segmentation for selective edits, and true content-aware fill/inpainting — both to be added using open source, on-device AI models (Apache-2.0 licensed), keeping everything local and GPL-compatible.

## 2. Goals

- Replace (or reduce dependence on) Lightroom for personal photo culling, organizing, and editing.
- Every edit is non-destructive and reversible at any time; originals are never modified.
- RAW files are first-class citizens, not an afterthought.
- AI-assisted selection (people/sky/background) makes local adjustments fast without manual masking.
- Exporting a shoot to shareable JPEGs is a one-click, batch operation.
- Runs entirely offline, entirely on-device — no cloud dependency, no subscription, no telemetry.

## 3. Non-Goals (v1)

- Not a multi-user or team/collaboration tool.
- Not a mobile app or web app — macOS only (though the upstream base is cross-platform, this project's focus is macOS).
- Not rewriting darktable's core RAW pipeline — reuse it; scope is additive (new modules/features) rather than a ground-up rebuild.
- Not building a generative AI content tool beyond content-aware fill/inpainting (no text-to-image generation, no sky replacement generation).
- Cloud sync/backup is out of scope for v1 (see Future Considerations).

## 4. Target User

A single user: a photography enthusiast with a software engineering background, shooting RAW (e.g., mirrorless/DSLR) plus phone JPEGs, who wants a fast, private, local-first alternative to Lightroom for personal photo management.

## 5. Key Assumptions & Constraints

Based on discussion, the following decisions frame this PRD:

- **Fully open source, and built by forking an existing open source project** rather than starting from scratch — so the code can be published on GitHub and contributed back upstream where useful (see Section 8a).
- **RAW support is essential.** Primary shooting format is **Canon CR2/CR3**, which darktable already supports as part of its 400+ camera RAW format coverage — inherited for free from the fork base. Other formats (NEF, ARW, DNG, etc.) are covered incidentally but aren't the priority to validate.
- **AI features run on-device**, using open source models (see Section 8b) rather than proprietary cloud APIs, prioritizing privacy and offline use. No cloud calls involving photo content.
- **Library uses a catalog model** (like Lightroom): the app maintains its own database of imported photos, edit history, ratings, and metadata, separate from the original files on disk. Originals are referenced, never modified in place. (The fork base already implements this.)
- **macOS only** as the primary target, targeting Apple Silicon Macs to take advantage of the Neural Engine — though the upstream project is cross-platform, so this isn't a hard fork-level constraint.
- All new/modified code stays under the upstream project's open source license (copyleft — see Section 8c), and is intended to be contributed back via pull requests, not kept as a permanently private fork.
- Scope is being phased — see Section 9 (MVP Phasing) — rather than shipping all features simultaneously.

## 6. Functional Requirements

### 6.1 Import & Organization

- Import photos from a folder, memory card, or connected camera/volume.
- Auto-organize into a library structure grouped by **import date** (e.g., Year/Month/Day, mirroring Lightroom's "By Date" view).
- Deduplicate on import (detect already-imported files, e.g., by checksum or filename+capture-time).
- Preserve original EXIF/capture metadata (capture date distinct from import date — both should be viewable/filterable).
- Basic library browsing: grid/thumbnail view, filmstrip, full-screen loupe view.
- Color labels supported as a secondary organization tool.
- Search/filter by date, rating, keep/delete status, and basic metadata (camera, lens, focal length).
- See Section 6.2 for the keep/delete triage and star-rating workflow.

### 6.2 First-Pass Triage: Keep vs. Delete, and 5-Star Rating

Two distinct, complementary culling tools, matching a typical import workflow: a fast first pass to cut obvious rejects, followed by finer-grained rating of the keepers.

**Keep vs. Delete triage (first pass):**

- After import, step through newly-added photos one at a time (loupe/full-screen view, keyboard-driven — e.g., single key for "keep," single key for "delete," arrow keys to advance).
- Marking "delete" flags the photo for removal but does **not** immediately erase it — it's added to a pending-delete list/filter so the pass can continue uninterrupted.
- At the end of the triage session (or on demand), review the pending-delete list, then **confirm** to delete: confirmed deletes are moved to the **macOS Trash** (not unlinked immediately), giving a recovery window via Finder until Trash is emptied. This was chosen over an immediate hard-delete as a safer default while the workflow is new; revisit once it's proven out.
- Confirmation step is mandatory and shows a count/preview of what will be deleted before it happens (e.g., "Move 14 photos to Trash? You can recover them from Trash until it's emptied."). No silent or single-click deletes.
- Deleting a photo also removes its catalog entry and edit history; the associated sidecar/XMP file moves to Trash alongside the original.
- RAW+JPEG pairs (if shooting both) are deleted together as a pair, not orphaned individually.

**5-star rating (beyond keep/delete):**

- Independent of the keep/delete decision: every "kept" photo can additionally be rated 0–5 stars (standard Lightroom-style scale) to grade quality/favorites among the keepers.
- Rating is a simple keyboard shortcut (1–5 keys) or click-on-star UI, available in loupe and grid views.
- Library is filterable/sortable by star rating (e.g., "show only 4+ star photos") independently of keep/delete status, date, or flags.
- Star rating has no effect on file deletion — it's purely an organizational/quality signal for photos already kept.

### 6.3 Non-Destructive Editing Engine

- All edits stored as instructions/metadata layered over the original file (edit stack), never modifying source pixels.
- Full edit history per photo, with ability to revert to original or any prior edit state at any time.
- Before/after comparison view.
- Edits defined in this PRD apply to both RAW and JPEG sources, with RAW naturally supporting more latitude (e.g., highlight/shadow recovery).

### 6.4 Crop & Align

- Freeform and fixed-aspect-ratio cropping.
- Straighten/rotate tool (manual angle adjustment).
- Auto-align: automatic horizon/perspective straightening (e.g., via horizon line detection).
- Non-destructive — crop boundaries stored as metadata, adjustable/undoable at any time.

### 6.5 Histogram Editing

- Live histogram display (RGB and luminance).
- Tone controls: exposure, contrast, highlights, whites, shadows, blacks (Lightroom-equivalent basic panel).
- Tone curve tool for finer control.
- White balance adjustment (temperature/tint), with RAW-aware white balance when source is RAW.

### 6.6 Shadow/Highlight Recovery

- Dedicated shadow-lift and highlight-recovery controls, leveraging RAW dynamic range where available.
- Local dehaze/clarity-style adjustment (optional, if feasible within engine).

### 6.7 Auto Recognition: People vs. Background vs. Sky

- On-device segmentation that automatically identifies distinct regions in a photo: people (subjects), sky, and background/other.
- Segmentation results are stored as selectable masks tied to the photo (regenerable if the algorithm improves later).
- Should handle common cases gracefully (single/multiple people, no-sky landscape shots, indoor scenes with no sky) and degrade sensibly when a category isn't present.

### 6.8 Selective Editing by Segment

- Ability to apply the full tone/color adjustment set (from 6.5/6.6) to a specific auto-detected segment only (e.g., "brighten the person," "deepen the sky's blue," "recover background detail").
- Ability to view/toggle mask overlays to confirm what's selected before adjusting.
- Ability to manually refine an auto-generated mask (brush add/subtract) for cases where AI selection isn't perfect.
- Multiple simultaneous local adjustments stacked (e.g., sky adjustment + separate person adjustment on the same photo).

### 6.9 Select & Erase with Content-Aware Fill

- Manual selection tool (brush/lasso) to mark an area for removal (e.g., distracting object, sensor dust, power line).
- On-device content-aware fill/inpainting that intelligently reconstructs the erased area based on surrounding image content.
- Non-destructive: the fill is a layer/operation that can be removed or redone, original pixels retained underneath.
- Note: this is the most technically ambitious feature in the app — see Section 10 (Risks) regarding on-device inpainting model availability/quality.

### 6.10 Mass Export to JPEG

- Batch-select any number of photos and export all applied edits "baked in" to JPEG.
- Export settings: resolution/long-edge sizing, JPEG quality/compression, color space (sRGB/Adobe RGB), optional watermark, optional metadata stripping (e.g., remove GPS before sharing).
- Configurable output filename pattern and destination folder.
- Background/async export with progress indicator so the app remains usable during large batch exports.

## 7. Non-Functional Requirements

- **Performance:** Editing (especially RAW preview rendering and local AI adjustments) should feel real-time/interactive on Apple Silicon; avoid noticeable lag when dragging sliders.
- **Data integrity:** Original files are never modified by the app, and are only ever deleted via the explicit, confirmed keep/delete triage flow (Section 6.2) — never as a side effect of any other action. Catalog database is backed up or at minimum warns the user to back it up (loss of catalog = loss of edit history, though originals remain safe unless explicitly deleted).
- **Storage:** Current library is in the 5,000–20,000 photo range; the app should handle this comfortably today and scale gracefully as it grows, without significant slowdown. Thumbnail/preview cache managed efficiently.
- **Privacy:** No network calls involving photo content; all AI inference local.
- **Reliability:** Crash recovery — in-progress edits shouldn't be lost if the app quits unexpectedly.

## 8. Technical Approach: Open Source Fork Strategy

### 8a. Choice of fork base

Two mature open source candidates were evaluated:

| Project | Strengths | Fit for this PRD |
|---|---|---|
| **darktable** | Non-destructive, 32-bit float, GPU-accelerated RAW pipeline; 400+ camera formats; parametric + drawn masks and blend modes on every module already; catalog/library ("lighttable") with collections/film-rolls that can group by import date; runs on macOS (Apple Silicon) with CoreML acceleration bundled; as of v5.6 already ships an early **AI object mask tool** and a **neural restore module** (AI denoise/upscale) — i.e., the project has already started down the same on-device-AI path this PRD wants. | **Recommended primary fork base.** Closest match to the non-destructive editing + masking requirements (Sections 6.3–6.8). |
| **digiKam** | Strong catalog/library tool with mature AI **face recognition**, tagging, and organize-by-date workflows; cross-platform incl. macOS. | Weaker non-destructive RAW *editing* engine than darktable. Good reference for face/people workflows, but not the recommended core editing base. |

**Recommendation:** Fork **darktable** (github.com/darktable-org/darktable) as the primary base for the editing engine and catalog. It already covers Sections 6.1 and 6.3–6.6 out of the box or very close to it. Section 6.2 (keep/delete triage + 5-star rating) needs verification against darktable's existing rating/reject flow, plus the permanent-delete-from-disk behavior specifically (darktable's default reject flow may not delete from disk by default — see Section 10). Development effort concentrates on Sections 6.7–6.9 (AI segmentation, selective editing by segment, content-aware fill) plus any personal workflow polish (e.g., default "by import date" collection view) and Section 6.10 (batch JPEG export, which darktable also already supports and mainly needs configuration/verification, not new code).

### 8b. AI model integration (open source, on-device)

- **People/subject segmentation:** [Segment Anything 2 (SAM 2)](https://github.com/facebookresearch/segment-anything-2) — Apache 2.0 license, Meta/FAIR. Apple publishes ready-made Core ML conversions (`apple/coreml-sam2-*` on Hugging Face) that run on the Apple Neural Engine, fully local. This can plug into darktable's existing AI object mask infrastructure (introduced in 5.6) or a new dedicated module.
- **Sky segmentation:** darktable/Vision don't ship a dedicated sky class; plan to either fine-tune a small open source semantic segmentation model for sky, or approximate it using SAM 2 prompted/heuristic selection (e.g., top-region + color/luminance priors) as a first pass. Flagged as a feasibility spike (Section 10).
- **Content-aware fill / inpainting:** [LaMa (Large Mask Inpainting)](https://github.com/advimman/lama) — Apache 2.0 license, resolution-robust, lightweight enough for on-device (CPU/GPU) use, with existing on-device ports. This maps directly onto an **open, unresolved darktable feature request** ([darktable-org/darktable#15006](https://github.com/darktable-org/darktable/issues/15006) — "retouch: add content-aware fill / inpainting mode") — i.e., there's an existing upstream community appetite for exactly this feature, making it a strong upstream-contribution candidate rather than a fork-only addition.
- All chosen AI models are Apache-2.0 licensed, which is compatible with darktable's GPL license for inclusion in the combined work.

### 8c. Licensing & contribution plan

- darktable is licensed under the **GPL** (GNU General Public License); any fork and distributed modifications must remain GPL-licensed and source-available — this aligns directly with the goal of building something contributable back to open source.
- Workflow: fork `darktable-org/darktable` on GitHub → develop features on branches in the fork → for features of general usefulness (notably the LaMa-based content-aware fill, which directly closes an existing upstream issue, and improvements to the AI object mask/segmentation module), submit pull requests upstream on a **best-effort basis** following darktable's contribution guidelines — but don't block personal use or roadmap progress on maintainer review cycles. The public fork itself (published on GitHub) is a fine long-term home for anything that doesn't get merged. Keep narrowly personal workflow tweaks (e.g., Trash-first delete behavior, default import-date collection view) in the fork regardless of upstream interest.
- AI model weights (SAM 2, LaMa) are redistributed under their own Apache 2.0 terms alongside the GPL codebase — standard practice for GPL projects bundling separately-licensed model assets; verify final packaging approach against darktable's existing precedent for its neural restore module (5.6) before release.

### 8d. Catalog & data storage

Inherited from darktable: a local SQLite-based library database storing photo references, edit history stack, ratings, and metadata; XMP sidecar files provide a portable, human-inspectable non-destructive edit "recipe" per photo. Originals are never modified.

## 9. MVP Phasing (Recommended)

**Phase 0 — Fork Setup & Baseline Audit**
Fork `darktable-org/darktable`, build it locally on Apple Silicon, and audit exactly which of Sections 6.1, 6.3–6.6, and 6.10 already work out of the box vs. need configuration/polish (e.g., default lighttable view grouped by import date). Specifically audit darktable's existing reject/delete flow against the permanent-delete-from-disk requirement in 6.2. Set up CI and a personal release/build pipeline.

**Phase 1 — Core Library, Triage, & Basic Editing**
Close any gaps found in Phase 0: import & organize by date, RAW decoding, non-destructive crop/align, histogram/tone editing, shadow/highlight recovery, mass export to JPEG. Build/wire up the **keep-vs-delete triage workflow with permanent disk delete + confirmation** and the **5-star rating** (Section 6.2) — treated as a Phase 1 priority, not deferred, since it's the first thing touched on every import.
→ Since most of the editing/export functionality is inherited from darktable, this phase's main net-new work is the triage/delete workflow; the rest should be short and mostly validation.

**Phase 2 — AI-Assisted Selective Editing**
Integrate SAM 2 (Core ML) for people/subject segmentation into (or alongside) darktable's existing AI object mask tool; build/evaluate a sky segmentation approach; wire segment masks into the existing per-module masking system so any tone/color adjustment can target a specific segment; add manual mask refinement.

**Phase 3 — Select & Erase / Content-Aware Fill**
Integrate LaMa inpainting into darktable's retouch module "fill" mode (addressing upstream issue #15006). Prepare this as an upstream PR candidate (best-effort). Placed last since it's the highest technical risk. In the meantime (Phases 0–2), darktable's existing **clone/heal tools** in the retouch module serve as the interim object-removal workflow, so this capability isn't fully blocked until Phase 3 lands.

**Phase 4+ (Future)** — see Section 11.

## 10. Risks & Open Technical Questions

- **Sky/background segmentation model:** No existing open source model ships a dedicated, reliable "sky" class comparable to person segmentation — will likely need a custom-trained or fine-tuned model, or a heuristic fallback on top of SAM 2. Needs a feasibility spike before Phase 2 is scoped.
- **On-device inpainting quality:** LaMa is lighter-weight than diffusion-based generative fill (e.g., Photoshop's Generative Fill) and won't match that quality on complex scenes — acceptable for object/blemish removal, less so for large complex fills. Needs a feasibility spike before Phase 3, with expectations set accordingly.
- **Upstream contribution fit:** darktable maintainers will review any PR against their own roadmap/standards (e.g., they may prefer a specific architecture for AI features, following on from the 5.6 neural restore module). Contribution timelines and acceptance aren't guaranteed — treat "upstreamed" as a stretch goal per feature, with the personal fork as the fallback distribution path regardless.
- **Build/toolchain complexity:** darktable is a C-based, GTK-based project (not a native Swift/AppKit app) — plan for a Linux/GTK-style build and packaging workflow on macOS (already supported, per SourceForge/GitHub macOS builds) rather than a typical Xcode-first app experience.
- **Catalog resilience:** Define backup strategy for the SQLite catalog/library file so edit history isn't lost if it corrupts (originals on disk remain safe regardless).
- **Delete safety:** Resolved — confirmed deletes move to macOS Trash rather than unlinking immediately (Section 6.2), giving a recovery window while the workflow is new. Still needs careful UX regardless (explicit confirm step, clear count/preview, no accidental bulk-delete via stray keypress).

## 11. Future Considerations (Explicitly Out of Scope for Now)

- iCloud/cross-device sync (e.g., companion iPad/iPhone app).
- Cloud backup of catalog/edits.
- Additional export targets (TIFF, PNG, print).
- Presets/preset marketplace.
- Face recognition/people-tagging across library (distinct from per-photo person segmentation).
- Geotagging/map view.
- Duplicate/near-duplicate detection across the whole library.

## 12. Decisions Log

All prior open questions have been resolved:

- **RAW format:** Canon CR2/CR3 is the primary target; darktable's existing format support covers it.
- **Library size:** 5,000–20,000 photos — sized as a "should perform comfortably today" target in Section 7.
- **UI direction:** Use darktable's existing darkroom/lighttable UI as-is; no Lightroom-style UI rework in scope.
- **Upstream contribution stance:** Best-effort upstream PRs where a feature is generally useful; not blocking on maintainer review — the public fork is a fine permanent home either way (Section 8c).
- **Interim object-removal fallback:** Yes — darktable's existing clone/heal tools cover this need in Phases 0–2, ahead of the LaMa content-aware fill in Phase 3.
- **Permanent delete behavior:** Confirmed deletes move to macOS Trash (recoverable) rather than unlinking immediately (Section 6.2, Section 10).
- **Next step:** Proceed to Phase 0 — fork `darktable-org/darktable` on GitHub, get it building, and start the baseline audit.

## 13. Open Questions for Next Discussion

None outstanding — revisit this section if new questions surface during Phase 0.
