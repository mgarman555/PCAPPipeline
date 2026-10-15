# Face detector for the HMC checker

**Status:** research only, no code yet (2026-10-15). Replaces the brightness-plus-edges "face present" test in `AnalyzeFrameBGRA`.

## What UE 5.8 ships

| Option | Where | Verdict |
|---|---|---|
| **MetaHuman face detector** | `Engine/Plugins/MetaHuman/MetaHumanCoreTechLib/Content/GenericTracker/FaceDetector` (an `NNEModelData` asset) | **Use this.** It is the detector MetaHuman Animator itself runs, so "face present" in the checker means "the solver will find a face". |
| MetaHuman realtime node | `FHyprsenseRealtimeNode` (`MetaHumanPipelineCore`, editor module) | Same detector plus head pose and solve. Reports `OK`, `NoFace`, `SubjectTooFar`. Heavier; worth it later for head-pose checks. |
| Small-face detector | `SmallFaceDetector`, under the MetaHuman Body plugin content | For faces far from the camera (body camera, not HMC). Only present once that plugin is installed. |
| OpenCV 4.5.5 | `Engine/Plugins/Runtime/OpenCV` | Ships no face model files. Would mean downloading a model. Not recommended. |

## How the MetaHuman detector is driven (from engine source)

- Model setup: `FMonocularAnimationPipelineModels::SetModelConfiguration` in `HyprsenseRealtimeNode.cpp`. The `Default` and `StereoHMC` configurations both use `GenericTracker/FaceDetector`.
- Input: 300×300 RGB, normalized `(pixel − 127) / std`; see `FHyprsenseUtils::WarpAffineBilinear(..., bInIsDetector = true)` in `HyprsenseUtils.cpp`.
- Output: scores plus boxes, filtered by `FHyprsenseUtils::HardNMS` (IoU and probability thresholds). Both helpers are exported (`UE_API`).
- Runtime: NNE with the `NNERuntimeORTCpu` backend, or `NNERuntimeORTDml` on the GPU (the engine's tested options on Windows).

## Plan

1. Add a `FaceDetect` skill behind the existing skill interface. It loads the `NNEModelData` asset once and runs on the preview frames at a low rate, about 5 per second per camera, off the game thread.
2. Keep the current heuristic as the fallback when the MetaHuman plugin or NNE runtime isn't available, and say so in the HMC Monitor.
3. Test with real footage: the generated test frames have no real face in them. Needs the OneHMC feed or a clip Madi records, extracted with FFmpeg, then thresholds tuned against Epic's guideline images.
