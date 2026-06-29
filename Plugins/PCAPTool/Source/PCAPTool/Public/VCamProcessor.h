#pragma once

#include "CoreMinimal.h"

class UPCAPVCamConfig;

// Non-serialized per-session vcam state. Lives in UPCAPVCamSubsystem; reset on (re)activation.
struct FPCAPVCamRuntimeState
{
    // Operator toggles
    bool bFlightMode   = true;
    bool bLockPosition = false;
    bool bLockRotation = false;
    bool bLockRoll     = false;
    bool bKillRoll     = false;
    bool bHold         = false;

    // Joystick state — driven by the deferred input layer; inert (defaults) until then.
    float   TranslationGain           = 0.5f;
    float   TranslationGainMultiplier = 1.0f;
    float   ZoomGain                  = 1.0f;
    float   ZoomGainMultiplier        = 1.0f;
    bool    bOverrideTranslationGain  = false;
    bool    bOverrideZoomGain         = false;
    FString ActiveMapping             = TEXT("STANDARD");

    // Current joystick translation/rotation/zoom rates, set by the input layer each frame;
    // the subsystem tick integrates them (rate * dt) via Accumulate{Navigate,NavigateRotation,Zoom}.
    FVector NavigateRate         = FVector::ZeroVector;   // X=U, Y=W, Z=V translation (units/sec)
    FVector NavigateRotationRate = FVector::ZeroVector;   // X=Roll, Y=Yaw, Z=Pitch (deg/sec)
    float   ZoomRate             = 0.f;                    // focal-length mm/sec

    // Last-known values for the lock/kill steps (captured while the lock is off).
    FVector LockedPosition = FVector::ZeroVector;
    FQuat   LockedRotation = FQuat::Identity;
    float   LockedRoll     = 0.f;   // degrees

    // Smoothed output (updated every tick). First frame seeds without interpolation.
    FVector SmoothedPosition = FVector::ZeroVector;
    FQuat   SmoothedRotation = FQuat::Identity;
    bool    bSmoothingPrimed = false;

    // Hold snapshot.
    FVector HeldPosition = FVector::ZeroVector;
    FQuat   HeldRotation = FQuat::Identity;
};

// WVCAM-equivalent transform processing. Pure functions — fully unit-testable.
class PCAPTOOL_API FPCAPVCamProcessor
{
public:
    // Runs the locked 14-step chain for one frame; returns the final camera transform.
    // Focal length is read separately (GetFocalLength) — it is not derived from Live Link.
    static FTransform Process(const UPCAPVCamConfig& Config, FPCAPVCamRuntimeState& State,
                              const FTransform& RawLiveLink, float DeltaSeconds);

    // Sets Config.Setup so the current optical pose becomes the origin; clears Navigate.
    static void ZeroSpace(UPCAPVCamConfig& Config, const FTransform& RawLiveLink);

    // Enter/exit hold. On enter, snapshots the current processed pose. On exit, re-solves
    // Setup so the live input maps to the held pose (reproduces WVCAM hold-release + its
    // known roll artifact).
    static void SetHold(UPCAPVCamConfig& Config, FPCAPVCamRuntimeState& State,
                        bool bEnabled, const FTransform& RawLiveLink);

    // The active focal length (passthrough from config this session).
    static float GetFocalLength(const UPCAPVCamConfig& Config);

    // Accumulates the joystick/d-pad translation rate into Config.Navigate (rate * dt).
    // bFlightMode: rate is camera-local (move where you're facing) using CameraRot; else
    // world axes. Matches the WVCAM "Rate | Speed" mechanism (PDF) — Navigate stacks over time.
    static void AccumulateNavigate(UPCAPVCamConfig& Config, const FVector& Rate,
                                   const FQuat& CameraRot, bool bFlightMode, float DeltaSeconds);

    // Accumulates the joystick rotation rate (Roll,Yaw,Pitch deg/sec) into Config.Navigate.Rotation
    // (rate * dt). WVCAM's Ruvw integrates camera-local rotation with Euler order XYZ; this is the
    // recoverable Euler-accumulation approximation (the exact native integration is unavailable).
    static void AccumulateNavigateRotation(UPCAPVCamConfig& Config, const FVector& RollYawPitchRate,
                                           float DeltaSeconds);

    // Integrates a focal-length speed (mm/sec) onto Config.ActiveFocalLength, clamped to the
    // config's [MinFocalLength, MaxFocalLength] (WVCAM native zoom integration + clamp).
    static void AccumulateZoom(UPCAPVCamConfig& Config, float ZoomSpeed, float DeltaSeconds);

private:
    // Step 2 of the chain, also the basis ZeroSpace/SetHold solve against.
    static FTransform ApplyAlign(const UPCAPVCamConfig& Config, const FTransform& Raw);
};
