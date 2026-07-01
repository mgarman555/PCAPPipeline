#pragma once

#include "CoreMinimal.h"

class ULevelSequence;
class AActor;
struct FVCamSmoothingSettings;

// Editor-only glue: applies the offline smoothing post-pass to a recorded VCam take.
// Finds the camera's transform section in the Level Sequence, resamples its double channels
// (Loc XYZ + Rot Roll/Pitch/Yaw) to the resampling fps, runs FVCamCurveSmoothing, and re-keys
// (cubic + auto tangents). No-op unless a smoothing method is selected. Non-destructive intent:
// only the transform channels are touched; lens channels are left alone.
class FVCamSequenceSmoother
{
public:
    // Returns true if a transform section was found and re-keyed.
    static bool SmoothRecordedVCam(ULevelSequence* Sequence, const AActor* CameraActor,
                                   const FVCamSmoothingSettings& Settings);
};
