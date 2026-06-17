# Capture Monitor — Pipeline-Aware Automatic Camera Checks (Design Spec)

**Date:** 2026-06-10 · **Status:** Implemented + extended (see "Additions since the original design" below) — thresholds pending rig tuning · **Component:** `Plugins/PCAPTool`

## Goal

A **fully automatic, always-on monitor** that watches each HMC camera feed and flags the issues that matter for the **active capture pipeline**. No human checklist — the tool checks the video itself, continuously. The MetaHuman HMC pipeline is the first check bundle; other pipelines (body mocap, etc.) are added later as new bundles without touching the monitor.

The operator's only setup action for the checks is defining **where the actor's face should be** (a framing reference, captured once after calibration). Everything else the tool detects on its own.

## What changed from the first draft

- **Removed:** the performer-prep checklist (hair, glasses, makeup, piercings) and all manual framing toggles. Things the camera can't see automatically are simply out of scope — no manual lists.
- **Added:** a **pipeline selector** (active pipeline → active checks + thresholds + framing rules), and a **captured framing reference** per device so the framing check is automatic.
- **Framing became automatic** (subject position/drift vs. the reference) instead of operator-tapped flags.

## Architecture: pluggable checker per pipeline

The monitor is a thin loop — *for each connected camera, run the active pipeline's checks against the latest frame's metrics, OR the resulting flags into the device's issue state.* A **pipeline** is just a bundle of `(active checks, thresholds, framing target/tolerances)`.

```
ECapturePipeline { MetaHumanHMC, /* future: ViconBody, OptiTrackBody, ... */ }

FPipelineCheckProfile {
    bool  bCheckSubject, bCheckFraming, bCheckFocus, bCheckExposure, bCheckLighting;
    float FocusMin;                 // var-of-Laplacian floor
    float BlownFracMax;             // overexposed when blown fraction exceeds this
    float MeanLumaMin;              // underexposed when mean luma below this
    float RegionSpreadMax;          // uneven lighting when region spread exceeds this
    FVector2D FramingTargetCenter;  // pipeline-ideal center, normalized 0..1
    float FramingSizeMin, FramingSizeMax;  // acceptable subject size fraction
    float FramingCenterTol;         // captured ref must land within this of target
    float FramingDriftTol;          // live subject may drift this far from the ref
}
GetPipelineProfile(ECapturePipeline) -> FPipelineCheckProfile   // constants for now
```

Active pipeline is **per-device** (resolved with Madi): set in Setup during shoot-day setup and persisted on `FHMCDeviceConfig.Pipeline` (default `MetaHumanHMC`). Conceptually it is the pipeline of the asset that device's actor is shooting; per-device generalizes cleanly when one stage mixes pipelines.

## Setup reference (hybrid: pipeline target + capture-current)

For each device, after calibration, with the actor framed correctly:
1. Setup shows the pipeline's **ideal framing target** as a guide overlay on each camera feed (from `FramingTargetCenter` / size range).
2. Operator hits **Set reference** → the analyzer's current subject centroid + size is snapshotted per camera into `FHMCFramingRef`. It is **validated against the pipeline target** (`FramingCenterTol`); if the captured framing is outside tolerance, Setup warns rather than locking a bad reference.
3. The reference is **persisted per device** (`FHMCDeviceConfig`). The monitor flags live drift from it.

```
FHMCFramingRef { bool bSet=false; FVector2D Center; float Size; }   // per camera, normalized
```

## The monitor checks (MetaHuman HMC pipeline)

Per camera, always-on, automatic — all feed the existing green/red bar + red status line via `GetEffectiveIssueFlags`:

1. **Subject present** — face in frame (existing brightness/skin heuristic, `FrameHasSubject`).
2. **Framing / drift** — live subject centroid + size vs. the captured `FHMCFramingRef`, within `FramingDriftTol` / size tolerance. Catches headset drift, face too high/low/off-axis, too close/far. *Active only once the reference is set.*
3. **Focus** — variance-of-Laplacian below `FocusMin`.
4. **Exposure** — luma histogram: blown fraction > `BlownFracMax` (over), or mean luma < `MeanLumaMin` (under).
5. **Lighting evenness** — half/quadrant luma spread > `RegionSpreadMax`.

**Boundary (deferred to a future pipeline-check addition):** fine landmark checks — lip-seal visible, inner eyelid visible, ≤30° rotation — genuinely need facial landmarks (MetaHuman Animator / ARKit), so they are *not* in this increment. The framing check catches *"the face moved out of where it should be,"* not *"the lip seal is occluded."*

## Data model

**Auto `EHMCIssueFlag` bits** (append-only; 9 = NoFace, 10–12 free):
- `HMC_Issue_OutOfFocus  = 1 << 10`
- `HMC_Issue_UnevenLight = 1 << 11`
- `HMC_Issue_FramingDrift = 1 << 12`
- Reuse existing `Overexposed` / `Underexposed` (now histogram-driven, OR'd with the existing device-exposure logic).

**Retired (no UI built):** the manual-checklist plan — `EHMCManualIssue` framing bits and the performer-prep mask are not used by this design. Existing `EHMCManualIssue` values stay in the type for save-compat but get no UI; they are superseded by the automatic framing check.

**Framing reference:** `FHMCFramingRef` per camera (2 per device), persisted on `FHMCDeviceConfig`.

**Active pipeline:** `ECapturePipeline ActivePipeline` on the subsystem, persisted.

**Analyzer output** per `"Device_Cam"`: `FHMCImageMetrics { float FocusScore, MeanLuma, BlownFrac, CrushedFrac, RegionSpread; bool bHasSubject; FVector2D SubjectCenter; float SubjectSize; }` + the derived auto-flag mask (post-hysteresis).

## The analyzer

- **Pure helpers in `UPCAPToolStatics`:** `FHMCImageMetrics AnalyzeFrameBGRA(const uint8* BGRA, int32 W, int32 H)` and `int32 MapMetricsToAutoFlags(const FHMCImageMetrics&, const FPipelineCheckProfile&, const FHMCFramingRef&)`.
- **Hook** in `UPCAPToolSubsystem::OnVideoFrameResponse` where the decoded BGRA already exists.
- **Downsample** by stride to ~96×72 grayscale; **throttle** to ~5 Hz per camera.
- **Metrics:** variance-of-Laplacian (focus); luma histogram (blown ≥250, crushed ≤5, mean); half/quadrant means → `RegionSpread = (max−min)/mean`; **subject centroid + size** = brightness/skin-weighted centroid and extent of the in-subject region, normalized 0..1.
- **Hysteresis:** each flag must hold ~1 s (≈5 samples) before setting and ~1 s before clearing — no blinking on a motion-blurred head-turn.
- **Gating:** image checks only when `bHasSubject`; framing check only when the reference `bSet`.

## Starting thresholds (conservative; tuned on the rig)

MetaHuman HMC `FPipelineCheckProfile`: `FocusMin` calibrated against a known-sharp feed (normalized by mean luma) · `BlownFracMax = 0.05` · `MeanLumaMin = 40/255` (lenient — slight under is fine) · `RegionSpreadMax = 0.25` · `FramingTargetCenter = (0.5, 0.5)` · `FramingSizeMin/Max` ≈ 0.45–0.85 of frame · `FramingCenterTol ≈ 0.10` · `FramingDriftTol ≈ 0.08`. Constants in `PCAPToolStatics.cpp`; Project-Settings exposure is a later step.

## Severity mapping

Bar is binary (`None`→green, else→red); `DeviceErrorText` shows red whenever severity ≠ `None`. `OutOfFocus`, `Overexposed`, `FramingDrift` = **Red**; `UnevenLight`, `Underexposed` = **Amber** (still read red on today's binary bar — fine, and ready if a tri-state bar returns).

## UI

- **Pipeline selector** — a per-device dropdown in the Setup detail pane → `SetDevicePipeline`. Default MetaHuman HMC.
- **Setup detail pane** — per device: the pipeline **framing-target guide overlay** on each feed, a **Set reference** button (with state: not set / set / current-framing-out-of-tolerance warning), and live read-outs of the auto checks (focus/exposure/lighting/framing) for diagnosis. *No checklist, no manual toggles.*
- **Preview** — bar + red status line already reflect `GetEffectiveIssueFlags` (auto flags flow through). Add a subtle *"framing reference not set"* hint when a connected device has no reference yet (framing check inactive until then).

## Error handling & states

- Guard null/empty frame, zero W/H; clamp fractions to [0,1].
- Image checks gated on subject; framing gated on reference set.
- Hysteresis prevents flag flicker.
- All on the game thread (cheap after downsample + throttle); no new threads.

## Testing

- `AnalyzeFrameBGRA` / `MapMetricsToAutoFlags` are pure — reviewable on synthetic buffers (uniform gray → no flags; all-255 → blown; half-bright → uneven; offset bright blob → framing drift vs. a centered ref).
- Real validation on Windows (Mac can't compile UE 5.7): defocus → focus flag; raise exposure → blown; side-light → uneven; slip the headset → framing drift.

## Deferred (slot in later, no rework)

- **Landmark checks** (lip-seal, eyelid, ≤30° rotation) as additions to the MetaHuman HMC pipeline profile once MHA landmarks are in the loop.
- **Stereo calibration RMS** scoring (bucket D).
- **Additional pipeline profiles** (body mocap, etc.) — the whole point of `ECapturePipeline`.

## Additions since the original design (current state)

Built on top of the original auto-monitor, all in the same MetaHuman HMC pipeline + HMC operator UI:

- **Mount stability** — `Bumped` (sudden subject-centroid jump → latched ~1.2 s, red) and `Unstable` (centroid std-dev over a ~2 s window → red), both gated on the pipeline's framing check; feed the **Frame** box. Computed in the subsystem from the centroid the analyzer already produces.
- **Low frame rate** — `LowFPS` when device `frameRate < 55` (margin under the docs' 60 fps); device-wide, surfaces via the feed border + status line (no dedicated box). VERIFY `frameRate` is the capture rate on the rig.
- **Directional framing** — `FramingDrift` appends the direction the face moved from the reference ("low / high / off-axis left …") via `UPCAPToolSubsystem::GetFramingHint`, in both panels' status text.
- **Setup status line** — a persistent red line under the Setup feeds writing out every active reason for the selected HMC (matches Preview). Strictly green/red, no "ready" state.
- **Two-column Setup detail** — exposure/gain/lights/boom on the left; Capture Monitor (pipeline picker + per-camera framing reference) on the right; the four check boxes sit under each camera's feed.
- **Prepped for Preview** — `FHMCDeviceConfig.bPreppedForPreview`; Preview shows only prepped devices; the bottom button marks all registered devices.
- **Pipelines** — `ECapturePipeline { MetaHumanHMC, FaceWareHMC }`. Faceware is its OWN pipeline that runs NO checks (all `bCheck* = false`) until its docs land; its check boxes render **grey** and read "not checked by this pipeline".
- **Actor sourcing** — the Setup actor dropdown lists only the active day's call sheet (`MocapDatabase` active day → `FShootDay.CalledActorIDs`), resolved to "First Last" via the Actor roster.
- **Per-check honest state** — a Setup check box renders **grey** (not green) when its check is inactive: the pipeline doesn't run it, or (Focus) `FocusMin` is still 0/untuned. Green now strictly means an *active* check is passing. Preview cards resolve the actor to "First Last" and show the device's pipeline under the IP, so a no-check Faceware feed is labelled rather than silently green.
- **Actor reassign resets the reference** — `AssignActor` clears the per-camera `FHMCFramingRef` + stability history (the reference was captured for the previous performer's face/mount, so it would mis-fire for the new actor).
- **Set-reference feedback** — the Setup reference line shows whether the captured reference is within the pipeline target ("within target", muted) or off it (red "reframe (off target)"), not just in the Output Log.

All image trip-points now live in `FPipelineCheckProfile` (`PCAPToolTypes.h`); `LowFPS` is in `EvaluateCameraIssues`. Severity is strictly binary on the bar (any flag → red); the *reason* is always written in the status area.

## Operating it (build & tune) — for the next on-rig session

1. **Build:** on Windows, `git reset --hard origin/main`, close the editor, **Rebuild Solution** (Development Editor | Win64). The new types are header changes, so this needs a full rebuild, not Live Coding.
2. **Live immediately:** auto **exposure** (blown / under) and **uneven-lighting** detection — they drive the green/red bar + red status line with no setup.
3. **Tune focus:** `FocusMin` defaults to `0` (focus check off) so it can't nuisance-flag before tuning. Select a device in HMC **Setup** and read the live `focus` value in the camera read-out (also logged ~1×/sec as `[PCAPTool] HMC … | focus …`). Note it sharp vs. defocused, pick a `FocusMin` between, set it in `FPipelineCheckProfile` (`PCAPToolTypes.h`), rebuild. Calibrate against frames at the **real operating brightness** (the score weakens at very low luma).
4. **Arm framing:** with the actor correctly framed (face on the faint centre crosshair), hit **Set reference** per camera in Setup. The Output Log reports whether the capture was within the pipeline target. From then on, drift from that reference raises a red **Frame** flag; Preview shows "Framing reference not set" until you do this.
5. **Thresholds** (image ones all in `FPipelineCheckProfile`, `PCAPToolTypes.h`): `FocusMin`, `BlownFracMax` (0.05), `MeanLumaMin` (40/255), `RegionSpreadMax` (0.25), `FramingDriftTol` (0.08), `FramingSizeMin/Max` (0.45–0.85), `BumpJumpMin` (0.06), `InstabilityStdMax` (0.03), `BumpHoldSeconds` (1.2); plus `LowFPS` `< 55` in `EvaluateCameraIssues` (`PCAPToolStatics.cpp`). The read-out's raw numbers are the tuning reference.
