# HMC Capture Watcher — Per-Pipeline, Per-Configuration Definition

**Date:** 2026-06-16 · **Status:** Design approved → implementing · **Component:** `Plugins/PCAPTool` (HMC operator UI + Capture Monitor)
**Supersedes** the framing-overlay direction in `Docs/2026-06-10-hmc-capture-checks-design.md` (the automatic checks there remain the engine).

## Contents
1. Goal & model
2. Capture-configuration axis
3. The catalog — what to look for (LIVE / PREP / AUX)
4. Detection tiers
5. Features (A clean feed + definition · B focus helper · C scan-readiness gate)
6. Data model & code mapping
7. Implementation phases
8. Decisions · 9. Scope · 10. Sources

---

## 1. Goal & model

A **clean live feed** when watching an HMC — image plus a one-line status only, never an overlay drawn on the face. The intelligence sits behind it as a **definition authored per pipeline × capture configuration**: the catalog of every GOOD target and every BAD case, how each is detected, and the doc it comes from. The watcher loads the active definition and classifies each frame; the feed only shows a green `● GOOD` or red `● <reason>`.

- **Watching (Preview)** = clean feed + status line. No box/lines/reticle (those were tried and rejected — they can't align without landmark tracking).
- **Setup** = where the definition is *visible* (reference rubric + numeric read-outs + the scan-readiness gate). Its feed is clean too; diagnostics sit beside it.
- **Per pipeline × configuration** = a definition. `Pipeline` (MetaHuman HMC, Faceware…) and `CaptureConfiguration` (mono-tripod / mono-headmount / stereo-headmount) together select it. MetaHuman HMC is fully defined here; Faceware stays empty until its docs land.

---

## 2. Capture-configuration axis

The MetaHuman pipeline branches by **camera setup** — the watcher must know which is active because the checks differ. Add `ECaptureConfiguration { MonoTripod, MonoHeadMount, StereoHeadMount }`.

| Aspect | Mono · Tripod (iPhone) | Mono · Head-Mount (iPhone) | Stereo · Head-Mount |
|---|---|---|---|
| Cameras / depth | 1, external · IR TrueDepth | 1, on-head · IR TrueDepth | 2, on-head · stereo disparity |
| Framing | head & shoulders; face the camera (not the screen); minimize body movement | tight face, fills frame | tight face, fills frame |
| Position cue | "camera too high/low" → move camera | "performer too high/low/off-axis" | "performer too high/low/off-axis" |
| Distance | Depth Preview: gray = correct, **black = too close/far** | FOV accommodates some head movement | within limited DOF |
| Focus / DOF | nasolabial sharp · AF off | nasolabial sharp · AF off | limited DOF → nasolabial is the priority plane |
| Calibration | identity (neutral/teeth/ROM) | identity | identity **+ stereo board take** (start & end) |
| Stability | camera fixed on tripod; performer holds position | camera stable on the face | camera stable on the face |
| Background | uniform, **darker than the face** | rig fills it | rig fills it |
| **Shared (all)** | centered & parallel · lip seal + inner upper eyelid visible · ≤30° rotation · exposure (slight-under OK) · ≥60fps ideal (**30 now**) · no occlusion/glasses/heavy-makeup/piercings · ≤1–2 day stubble | | |

Configuration-specific checks (distance/depth, board calibration, body-movement, background) activate only for the relevant config.

---

## 3. The catalog — what to look for

### ① LIVE — judged continuously on the clean feed

| Category | GOOD | BAD cases | Detection | Tier |
|---|---|---|---|---|
| Framing / pose | centered, parallel, fills frame; slightly-low upward camera; lip seal + inner upper eyelid visible; ≤30° | out of frame · too high · too low · off-axis >30° · too close · too far | subject position & size vs frame (rotation approx) | AUTO (LANDMARK later) |
| Lighting | uniform, shadow-free, predominantly frontal; visible > IR; color > mono | **over · under · lit-from-below · lit-from-side · back-lit · shadows-present** · changing ambient | region/quadrant luma + *where* the brightness sits + temporal change | AUTO |
| Exposure | well-exposed; slight under fine | blown/over (worst) · too dark/under | blown-pixel fraction + mean luma | AUTO |
| Focus | sharp on the nasolabial area | defocused / blurry · AF hunting | variance of Laplacian, weighted to the nasolabial region | AUTO |
| Stability | camera fixed & stable on the face | sudden bump · slow slip/drift | centroid jump + variance over time | AUTO |
| Distance | correct depth (gray) / face size in range | too close · too far (depth black artifacts; or size) | depth preview (mono-tripod) or subject size (HMC) | AUTO/DEVICE |
| Frame rate & frames | ≥60 ideal (**30 now**); no drops; high res; color | below target fps · dropped frames · auto-exp/AF ON · low res | device frameRate / dropped / resolution | DEVICE |

### ② PREP — must pass before "ready to scan"

| Category | GOOD | BAD | Detection | Tier |
|---|---|---|---|---|
| Performer prep | ≤1–2 day stubble; nothing occluding | glasses, hats, long hair, masks, hands; heavy makeup; piercings | operator checklist (occlusion auto later) | OPERATOR/LANDMARK |
| Identity calibration | **Neutral** (relaxed, teeth touching not clenched, forward, no tilt) · **Teeth** (bared, incisor corners + max surface) · **ROM** (expressions isolated, forward, clean) — MH1/MH12/MH50 | missing neutral/teeth · blurry · head tilt/forward during pose · noisy/un-isolated | capture-state gate + reuse live focus/blur + framing on captured frames | OPERATOR + AUTO |
| Stereo board calibration *(stereo only)* | flat matte rigid board, sharp corners, no chips; start **and** end of session; "paint the space" up/down/left/right/roll/pitch ~20s in the performer's plane | too close/far · out of frame · occluded by hand · hand visible · inverted · horizontal · over-rotated · actor present | board-framing classifier in a "calibration take" mode | AUTO/DEVICE |

### ③ AUXILIARY
- **Background** — uniform & darker than the face (mono-tripod). · **Audio** — ≥16 kHz, high SNR, no reverb/echo, one speaker per file.

---

## 4. Detection tiers
**AUTO** = from pixels now (`AnalyzeFrameBGRA`) · **DEVICE** = telemetry (fps, drops, resolution, depth) · **OPERATOR** = checklist · **LANDMARK** = future (lip-seal/eyelid occlusion, true head-pose; needs validated face tracking — standard RGB models may not survive IR HMC, evaluate on real frames).

---

## 5. Features

- **A — Clean feed + per-pipeline/config watcher definition.** Preview = clean feed + status; watcher resolves `Pipeline × Configuration` and classifies. Core of this spec.
- **B — Focus calibration helper.** Setup control: *Capture sharp* / *Capture soft* → read the live focus metric, propose `FocusMin` between them, save to the active profile. Turns the dormant focus check on with a click on the rig (no code edit/rebuild).
- **C — Per-actor scan-readiness gate.** Per-actor state: performer-prep confirmed, neutral captured, teeth captured, ROM captured. **Neutral & teeth saved as stills** from the live stream (loaded back as thumbnails); ROM is a device take, marked + thumbnailed. "Ready to scan" only when prep + neutral (+ teeth) + ROM present *and* LIVE checks green. This is the "always prepped for the active pipeline" assistant.

---

## 6. Data model & code mapping

The analyzer already yields the LIVE auto signals — this work is mostly a richer definition + presentation + the prep gate, not new math.

- **`FHMCDeviceConfig` (`PCAPToolTypes.h`)** → add `ECaptureConfiguration CaptureConfig` + scan-readiness fields (`bPerformerPrepConfirmed`, `bNeutralCaptured/bTeethCaptured/bROMCaptured`, artifact paths). Persisted in Save/LoadConfig.
- **`ECapturePipeline`** stays; **`GetPipelineProfile`** → `GetDefinition(Pipeline, CaptureConfig)` returning the resolved `FPipelineCheckProfile`.
- **`FPipelineCheckProfile`** → add `TargetFPS` (MetaHuman = 30 now), a **focus region weight** (nasolabial band), and lighting-direction enables. The `<55` fps test moves out of `EvaluateCameraIssues` into the profile.
- **`AnalyzeFrameBGRA` (`PCAPToolStatics.cpp`)** → (a) weight the variance-of-Laplacian to the centre/lower-centre (nasolabial) band; (b) classify **lighting direction** from quadrant/region luma (bottom→below, one-side→side, edges-bright+centre-dark→back-lit, high local contrast→shadows) in addition to the existing spread.
- **`EHMCIssueFlag`** → existing bits cover most; add `TooFar` (size under min) and a lighting-direction detail (enum or sub-flags) for the status reason. `LowFPS` already exists.
- **Preview (`SHMCPreviewPanel`)** → remove framing overlay; clean feed + one status line (`GetIssueBannerText` + `GetFramingHint`).
- **Setup (`SHMCSetupPanel`)** → remove crosshair/oval overlay from the feed; keep numeric read-out; add the **definition reference rubric**, the **scan-readiness gate** UI, and the **focus helper** control. Add a **CaptureConfiguration** picker beside the pipeline picker.

---

## 7. Implementation phases (author on Mac → build on Windows → tune on rig)

1. **Data model + config axis** — `ECaptureConfiguration`, profile additions (`TargetFPS`, focus region, lighting-direction enables), `GetDefinition(Pipeline, Config)`, persistence. *(types/statics — brace-checkable)*
2. **Analyzer** — nasolabial-weighted focus + lighting-direction classification + distance/size. *(statics)*
3. **Clean Preview** — strip overlay, status-only.
4. **Setup** — config picker + definition rubric + remove overlay.
5. **Scan-readiness gate (C)** — state, still capture/persist (neutral/teeth), ROM marking, "Ready to scan".
6. **Focus helper (B)** — capture sharp/soft → propose `FocusMin`.
7. **Stereo board calibration mode** — calibration-take classification (stereo config).

Each phase: author → brace/symbol-check → subagent compile-review → commit. Verification is the Windows build + on-rig tuning (no local UE 5.7 toolchain; no runnable unit tests for Slate/subsystem here).

---

## 8. Decisions
- ✅ **Watcher intelligence = #1 heuristic now** (robust on IR, no deps, reuses `AnalyzeFrameBGRA`), built behind an interface so **#2 ML/landmark** can slot in later (evaluate a model on real HMC IR frames first).
- ✅ **Stereo board calibration — in scope** (StereoHeadMount config).
- ✅ **Identity stills — neutral AND teeth saved**; ROM = device take + thumbnail.
- ✅ **Target fps = 30 now** (60 ideal), per pipeline.
- ⏳ **Faceware** — definition empty until its docs land (its own good/bad sets; no inheritance).

## 9. Scope boundaries
- **Landmark-level auto checks** (lip-seal/eyelid occlusion, true head-pose off-axis) are future; operator-judged until face tracking is validated on real HMC IR frames.
- **Audio/background** are catalog entries monitored where data exists, not v1 blockers.
- **No live overlay drawing** anywhere — clean feed is a hard requirement.

## 10. Sources
[Using a Head Mount](https://dev.epicgames.com/documentation/en-us/metahuman/using-a-head-mount) · [Using a Stereo Head Mount](https://dev.epicgames.com/documentation/en-us/metahuman/using-a-stereo-head-mount) · [Using a Tripod](https://dev.epicgames.com/documentation/en-us/metahuman/using-a-tripod) · [Realtime Animation Guidelines](https://dev.epicgames.com/documentation/en-us/metahuman/realtime-animation-guidelines) · [Capture Device Requirements](https://dev.epicgames.com/documentation/en-us/metahuman/capture-device-requirements) · [Performer Requirements](https://dev.epicgames.com/documentation/en-us/metahuman/performer-requirements) · [Expressions for Likeness Calibration](https://dev.epicgames.com/documentation/en-us/metahuman/expressions-for-likeness-calibration) · [Calibration Takes](https://dev.epicgames.com/documentation/en-us/metahuman/calibration-takes) · [Audio Capture Recommendations](https://dev.epicgames.com/documentation/en-us/metahuman/audio-capture-recommendations) · [Known Issues 5.7](https://dev.epicgames.com/documentation/en-us/metahuman/known-issues-5-7)

**Doc image IDs** (prefix `https://dev.epicgames.com/community/api/documentation/image/`):
HMC framing — optimal `96a45192-4857-41a5-acb2-c86c475d76ad` · out `7c150985-…` · high `678daca1-…` · low `36b64775-…` · off-axis `c8d1e772-…` · close `b60c8a9c-…`.
Tripod lighting — optimal `a9aed456-…` · over `c4204603-…` · under `a0da7de9-…` · below `19e4c40b-…` · side `eeff0145-…` · back `dd332f90-…` · shadows `0f31d9bf-…`; distance ok `cea00cf8-…`.
Calibration board — board `af647ba6-…` · close `f0e71bb5-…` · far `4950a59a-…` · out `0d1c16dd-…` · occluded-hand `fa749645-…` · hand `ad53f28f-…` · inverted `475b9f87-…` · horizontal `64bcc1d0-…` · over-rotated `2dc1edbb-…` · with-actor `a7920fe1-…`.
