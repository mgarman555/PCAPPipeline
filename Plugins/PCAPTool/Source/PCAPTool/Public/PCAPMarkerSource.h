#pragma once

#include "CoreMinimal.h"

// One labeled prop/subject's tracked points (UE world space, cm, pre-alignment).
struct FVizMarkerGroup
{
    FName SubjectName;
    TArray<FVector> Points;
};

// A single frame of everything tracked this tick.
struct FVizFrame
{
    TArray<FVizMarkerGroup> Labeled;
    TArray<FVector> Unlabeled;
    void Reset() { Labeled.Reset(); Unlabeled.Reset(); }
};

// Pluggable feed (Live Link now, Vicon SDK later) — fills an FVizFrame each tick.
class IMarkerSource
{
public:
    virtual ~IMarkerSource() {}
    virtual bool IsAvailable() const = 0;
    virtual bool Connect(const FString& Host) { return true; }   // SDK only; no-op for Live Link
    virtual void Disconnect() {}
    virtual void Poll(FVizFrame& OutFrame) = 0;
};

namespace PCAPViz
{
    // Deterministic, distinct color per subject name (string hash → hue, fixed S/V).
    PCAPTOOL_API FLinearColor SubjectColor(FName SubjectName);

    // Vicon DataStream marker (mm, Z-up, right-handed) -> UE (cm, Z-up, left-handed).
    // Candidate mapping; the exact axis/flip is verified on-rig in Phase 2.
    PCAPTOOL_API FVector ViconMMToUE(double X, double Y, double Z);
}
