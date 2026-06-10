# HMC Capture Checks — Design Spec

**Date:** 2026-06-10 · **Status:** Approved (pending spec review) · **Component:** `Plugins/PCAPTool`

## Goal

Translate Epic's MetaHuman facial-capture documentation into the concrete set of conditions the HMC monitoring tool detects and surfaces. The tool should **auto-detect** image-quality issues from the live video (focus, exposure, lighting) and provide an **operator checklist** for the conditions a camera can't sense on its own (framing/coverage, performer prep). Everything feeds the green/red feed bar + red status line already built; performer prep instead drives a separate amber readiness chip.

This is the showcase-starting increment. Error *codes* and per-rig threshold tuning come later; landmark-based auto-framing and stereo-calibration scoring are explicitly deferred.

## Source requirements (from the docs)

- **A · Performer prep** (`performer-requirements`): facial hair ≤ 1–2 days stubble; no glasses/sunglasses; minimal makeup, no face paint; no facial piercings; no obstructions (hats, loose long hair, masks, hands); adult faces only.
- **B · Framing & coverage** (`using-a-head-mount`, `using-a-stereo-head-mount`, `realtime-animation-guidelines`): face centered & parallel, rotation ≤ ~30° (fails once outer eyebrow or mouth corner is hidden); stereo centers on the upper philtrum (base of nostrils); lip seal **and** inner upper eyelid visible; not too high / low / off-axis / close; camera stable vs. face (no drift); nothing occluding the head.
- **C · Image quality** (same sources): even, frontal, shadow-free light; slight underexposure preferred over over; no blown highlights, side-light, or visible room lights; in focus on the nasolabial/cheek area.
- **D · Stereo calibration quality** (`generate-calibration`): checkerboard sharp & fully in frame, varied poses, good coverage, enough frames; RMS reprojection error bands (Excellent ≤0.3px … Bad ≥1.5px). **DEFERRED.**

## Scope decisions (confirmed with Madi)

| Bucket | Mechanism | In this increment? |
|---|---|---|
| C — focus, exposure, lighting evenness | **Auto** (per-frame image analysis) | ✅ |
| B — framing/coverage, drift, occlusion | **Manual** operator flags | ✅ |
| A — performer prep (6 items) | **Manual** confirm checklist | ✅ |
| D — stereo calibration RMS | — | ⛔ deferred |
| Landmark auto-framing (replaces B manual) | CV/ML | ⛔ deferred |

- **Performer prep failures drive an amber `NOT READY` chip, NOT the red feed bar** — a beard/glasses is a known persistent limitation you shoot anyway, not a live error to fix, so it must not sit red all shoot.
- **Operator sets the manual framing flags in the Setup detail pane** (beside that HMC's big feed), not on the live Preview cards. Preview stays an at-a-glance monitor; Preview quick-flags can come later.

## Data model

**Auto image flags** — new `EHMCIssueFlag` bits (append-only; bits 10–11 are free, 9 = NoFace):
- `HMC_Issue_OutOfFocus  = 1 << 10`
- `HMC_Issue_UnevenLight = 1 << 11`
- Reuse existing `HMC_Issue_Overexposed` / `HMC_Issue_Underexposed`, now *also* driven by the histogram (OR'd with the existing device-exposure-value logic).

**Manual framing flags** — extend `EHMCManualIssue` (bit = `1 << (16 + index)`; **append only**, do not reorder existing 0–4 or saved state shifts):
- keep: `FaceOffAxis`(16), `HeadsetShift`(17), `OutOfFocus`(18, retired from UI — auto covers focus; bit kept for save-compat), `LipSeal`(19), `Eyelid`(20)
- add: `FramingTooHigh`(21), `FramingTooLow`(22), `TooClose`(23), `HeadOccluded`(24)

**Performer prep** — new, persisted per device. A 6-bit confirmed-mask on `FHMCDeviceConfig` (saved via `SaveConfig`), e.g. `int32 PrepConfirmedMask` with item enum `EHMCPerformerPrepItem { FacialHairOK, NoEyewear, MakeupOK, NoPiercings, FaceUnobstructed, AdultSubject }`. Readiness = all 6 confirmed.

**Analyzer output** — stored in the subsystem per `"Device_Cam"` key: latest `FHMCImageMetrics { float FocusScore; float MeanLuma; float BlownFrac; float CrushedFrac; float RegionSpread; }` plus the derived auto-flag mask (after hysteresis).

## The analyzer

- **Pure helpers in `UPCAPToolStatics`** (testable, no engine state): `FHMCImageMetrics AnalyzeFrameBGRA(const uint8* BGRA, int32 W, int32 H)` and `int32 MapMetricsToAutoFlags(const FHMCImageMetrics&)`.
- **Hook** in `UPCAPToolSubsystem::OnVideoFrameResponse`, right where the decoded BGRA already exists (before/after `UpdateFrameTexture`).
- **Downsample** by stride to ~96×72 grayscale (luma = 0.114B+0.587G+0.299R) — a few thousand samples, microseconds.
- **Throttle** to ~5 Hz per camera (skip frames by timestamp/counter); store the latest result.
- **Metrics:** variance-of-Laplacian on the small grayscale (focus); luma histogram → blown frac (≥250), crushed frac (≤5), mean; half (L/R) and quadrant means → `RegionSpread` (max−min)/mean (evenness / side-light / shadow).
- **Hysteresis:** each auto flag must hold for ~1 s (≈5 samples) before it sets, and clear for ~1 s before it clears — so a motion-blurred head-turn doesn't blink the bar.
- **Gating:** only evaluate when there is a current frame AND `FrameHasSubject` is true, so a black "No Feed" frame is never mislabeled underexposed.

## Starting thresholds (conservative; tuned on the rig)

- **Focus:** `FocusScore` below `FOCUS_MIN` for ~1 s → `OutOfFocus`. (`FOCUS_MIN` calibrated against a known-sharp feed; normalized by mean luma so a dark frame isn't read as soft.)
- **Overexposed:** `BlownFrac > 0.05` → `Overexposed` (blown highlights are unrecoverable — the docs' #1 enemy).
- **Underexposed:** `MeanLuma < 40/255` → `Underexposed` (lenient; docs say slight under is fine).
- **Uneven light:** `RegionSpread > 0.25` → `UnevenLight`.

Constants live in `PCAPToolStatics.cpp` for now; exposing them in Project Settings is a later step.

## Severity mapping

The feed bar is binary (`None` → green, else → red), and `DeviceErrorText` shows in red whenever severity ≠ `None`. Auto + manual framing flags slot into `GetIssueSeverity`: `OutOfFocus` and `Overexposed` are **Red**; `UnevenLight`, `Underexposed`, and the manual framing flags are **Amber** (still render red on the binary bar + red status text — acceptable, and ready if a tri-state bar returns later).

Performer-prep state is **not** part of `EHMCIssueFlag`, so it never affects the bar — it only computes the readiness chip.

## UI

**Setup detail pane** — new "Capture Checks" area under the existing feed/controls for the selected device:
1. **Performer prep** — 6 confirm rows + a `READY` / `N of 6 confirmed` summary. Writes `PrepConfirmedMask` (persisted).
2. **Live checks — auto** — read-only Focus / Exposure / Lighting indicators bound to the analyzer's latest metrics (`dot + label + state`), tagged `AUTO`.
3. **Live checks — manual framing** — toggle chips (off-axis, too-high, too-low, too-close, lip-seal-hidden, eyelid-hidden, head-occluded, headset-drift) → `SetManualIssue`. *(No manual-flag UI exists today; this builds it.)*

**Preview card** — bar + red status line already reflect `GetEffectiveIssueFlags` (auto + manual flow through automatically). Add a small **readiness chip** (`READY` / `NOT READY · N`) bound to `PrepConfirmedMask`.

## Components & boundaries

- `UPCAPToolStatics` — `AnalyzeFrameBGRA`, `MapMetricsToAutoFlags` (pure). Owns thresholds.
- `UPCAPToolSubsystem` — run analyzer on decode (throttled), store metrics + hysteresis state, expose `GetImageMetrics(Device,Cam)` / `GetAutoIssueFlags(Device,Cam)`, OR auto flags into `GetEffectiveIssueFlags`; prep get/set + persistence; mirror prep on `UHMCMonitorComponent` only if needed (Slate uses the subsystem).
- `SHMCSetupPanel` — Capture Checks UI (prep checklist, auto read-outs, manual toggles).
- `SHMCPreviewPanel` — readiness chip.

## Error handling

- Guard null/empty frame and zero W/H; skip analysis.
- Gate on `FrameHasSubject` to avoid black-frame false positives.
- Hysteresis prevents flag flicker; clamp all fractions to [0,1].
- All work on the game thread (cheap after downsample + throttle); no new threads.

## Testing

- `AnalyzeFrameBGRA` / `MapMetricsToAutoFlags` are pure functions — logically reviewable on synthetic pixel buffers (uniform gray → no flags; half-bright → uneven; all-255 → blown; checker pattern → high focus).
- Real validation on Windows (Mac can't compile UE 5.7): defocus a lens → focus flag; raise exposure → blown; side-light the rig → uneven; confirm prep chip + manual toggles drive the right indicators.

## Deferred (structured to slot in later)

- **D — stereo calibration RMS readout:** a separate calibration workflow surface; the RMS bands map cleanly to green/amber/red.
- **Landmark auto-framing:** replace the manual framing flags with MetaHuman-Animator/ARKit-derived landmark checks (lip-seal/eyelid visibility, ≤30° rotation, philtrum centering) once that data is in the loop.
