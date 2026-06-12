#include "VCamProcessor.h"
#include "VCamConfig.h"

FTransform FPCAPVCamProcessor::ApplyAlign(const UPCAPVCamConfig& Config, const FTransform& Raw)
{
    // Step 2: axis correction. Left-multiplied, consistent with Setup/Navigate.
    const FTransform Align(Config.AlignRigidBody.Rotation.Quaternion(), Config.AlignRigidBody.Translation);
    return Align * Raw;
}

FTransform FPCAPVCamProcessor::Process(const UPCAPVCamConfig& Config, FPCAPVCamRuntimeState& State,
                                       const FTransform& RawLiveLink, float DeltaSeconds)
{
    // 1-2: raw + align
    FTransform Cur = ApplyAlign(Config, RawLiveLink);
    FVector Pos = Cur.GetLocation();
    FQuat   Rot = Cur.GetRotation();

    // 3: Kill Roll — zero the roll component from the source
    if (State.bKillRoll)
    {
        FRotator R = Rot.Rotator();
        R.Roll = 0.f;
        Rot = R.Quaternion();
    }

    // 4: Lock Roll — freeze roll at last value (else record current roll)
    {
        FRotator R = Rot.Rotator();
        if (State.bLockRoll) { R.Roll = State.LockedRoll; Rot = R.Quaternion(); }
        else                 { State.LockedRoll = R.Roll; }
    }

    // 5: World-space scaling (position only)
    Pos *= Config.Scaling.WorldSpaceScale;

    // 6: Hold — substitute the held pose for the live one
    if (State.bHold)
    {
        Pos = State.HeldPosition;
        Rot = State.HeldRotation;
    }

    // 7: Lock Position — freeze position (live rotation continues)
    if (State.bLockPosition) { Pos = State.LockedPosition; }
    else                     { State.LockedPosition = Pos; }

    // 8: Lock Rotation — freeze rotation (live position continues)
    if (State.bLockRotation) { Rot = State.LockedRotation; }
    else                     { State.LockedRotation = Rot; }

    // 9: Setup offset (zero origin) — left-multiplied
    {
        const FTransform Setup(Config.Setup.Rotation.Quaternion(), Config.Setup.Translation);
        const FTransform Out = Setup * FTransform(Rot, Pos);
        Pos = Out.GetLocation();
        Rot = Out.GetRotation();
    }

    // 10: Navigate offset (joystick stacking) — left-multiplied
    {
        const FTransform Nav(Config.Navigate.Rotation.Quaternion(), Config.Navigate.Translation);
        const FTransform Out = Nav * FTransform(Rot, Pos);
        Pos = Out.GetLocation();
        Rot = Out.GetRotation();
    }

    // 11: Smoothing — position & rotation independent; frame-rate-independent. First frame seeds.
    if (!State.bSmoothingPrimed)
    {
        State.SmoothedPosition = Pos;
        State.SmoothedRotation = Rot;
        State.bSmoothingPrimed = true;
    }
    else
    {
        State.SmoothedPosition = Config.Smoothing.bSmoothPosition
            ? FMath::VInterpTo(State.SmoothedPosition, Pos, DeltaSeconds, Config.Smoothing.PositionSmoothing)
            : Pos;
        State.SmoothedRotation = Config.Smoothing.bSmoothRotation
            ? FMath::QInterpTo(State.SmoothedRotation, Rot, DeltaSeconds, Config.Smoothing.RotationSmoothing)
            : Rot;
    }

    // 12: Flight Mode — the basis for joystick (Navigate) deltas. No live joystick this session,
    // so Navigate is applied as a static offset above and flight mode is a no-op until the input
    // layer feeds per-frame deltas. Seam preserved (design §10).

    // 13-14: final transform (focal length read via GetFocalLength).
    return FTransform(State.SmoothedRotation, State.SmoothedPosition);
}

void FPCAPVCamProcessor::ZeroSpace(UPCAPVCamConfig& Config, const FTransform& RawLiveLink)
{
    // Make the current aligned pose the origin: Setup = ApplyAlign(Raw)^-1 so that
    // Setup * ApplyAlign(Raw) == Identity. (Corrects the handoff sample's quaternion
    // negation — q and -q are the same rotation — while preserving its intent.)
    const FTransform Cur = ApplyAlign(Config, RawLiveLink);
    const FTransform Inv = Cur.Inverse();
    Config.Setup.Translation = Inv.GetLocation();
    Config.Setup.Rotation    = Inv.GetRotation().Rotator();

    // Joystick stacking starts fresh.
    Config.Navigate.Translation = FVector::ZeroVector;
    Config.Navigate.Rotation    = FRotator::ZeroRotator;
}

void FPCAPVCamProcessor::SetHold(UPCAPVCamConfig& Config, FPCAPVCamRuntimeState& State,
                                 bool bEnabled, const FTransform& RawLiveLink)
{
    State.bHold = bEnabled;
    if (bEnabled)
    {
        // Snapshot the current processed pose as the held pose.
        State.HeldPosition = State.SmoothedPosition;
        State.HeldRotation = State.SmoothedRotation;
    }
    else
    {
        // Re-solve Setup so the live input now maps to the held pose:
        //   Setup * ApplyAlign(Raw) == Held  =>  Setup = Held * ApplyAlign(Raw)^-1
        const FTransform Cur  = ApplyAlign(Config, RawLiveLink);
        const FTransform Held(State.HeldRotation, State.HeldPosition);
        const FTransform Setup = Held * Cur.Inverse();
        Config.Setup.Translation = Setup.GetLocation();
        Config.Setup.Rotation    = Setup.GetRotation().Rotator();
    }
}

float FPCAPVCamProcessor::GetFocalLength(const UPCAPVCamConfig& Config)
{
    return Config.ActiveFocalLength;
}

void FPCAPVCamProcessor::AccumulateNavigate(UPCAPVCamConfig& Config, const FVector& Rate,
                                            const FQuat& CameraRot, bool bFlightMode, float DeltaSeconds)
{
    if (Rate.IsNearlyZero()) { return; }
    // Flight mode: the joystick axes are relative to where the camera is facing — rotate the
    // rate into world space by the current camera orientation. Otherwise the rate is world-axis.
    const FVector WorldDelta = (bFlightMode ? CameraRot.RotateVector(Rate) : Rate) * DeltaSeconds;
    Config.Navigate.Translation += WorldDelta;
}
