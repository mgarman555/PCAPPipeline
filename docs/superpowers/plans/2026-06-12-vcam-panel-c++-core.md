# VCAM Operator Panel — C++ Core — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the compile-safe C++ core of the VCAM virtual-camera operator system: the config DataAsset, the WVCAM-equivalent transform processor (unit-tested), the editor subsystem that reads `TPVCam` from Live Link and drives an in-engine Cine Camera every frame, the native camera actor, and the Take Recorder hookup that captures the camera into each take.

**Architecture:** Additive, pure C++. VCAM owns the *live* camera (Live Link → `FPCAPVCamProcessor` → `APCAPVCamActor`, driven by a `FTickableEditorObject` tick on `UPCAPVCamSubsystem`). The existing `UPCAPTakeRecorderSubsystem` owns *recording* — it arms the camera actor as a Take Recorder source and flags the take as having a vcam. No existing panel, take, or data-hierarchy type is modified destructively. Deferred (seams defined): the Slate panel, the joystick input layer, and session stream-status reporting.

**Tech Stack:** UE 5.7.4, plugin module type `Editor`. C++ `UDataAsset` / `UEngineSubsystem` + `FTickableEditorObject` / `ACineCameraActor`. Live Link via `ILiveLinkClient` modular feature. Take Recorder via the existing reflection-based source arming. Automation via `Misc/AutomationTest.h`.

**Build reality:** This machine has only UE 5.4; the project targets 5.7.4. **Madi runs every build/test checkpoint in her 5.7.4 environment**; the agent writes code and fixes from the error log. Keep batches small so the loop stays tight. Live Link frame accessors and the Take Recorder actor-source class path/property are "confirm-at-build" points (same risk class as the existing `AddLiveLinkSource`).

**Spec:** `docs/superpowers/specs/2026-06-12-vcam-panel-design.md`

**Branch:** `claude/happy-brahmagupta-d2127b`

---

## File Structure

**New files (8):**
- `Plugins/PCAPTool/Source/PCAPTool/Public/VCamConfig.h` — `UPCAPVCamConfig` DataAsset + `FPCAPVCamAlignOffset` / `FPCAPVCamSmoothingConfig` / `FPCAPVCamScaleConfig`
- `Plugins/PCAPTool/Source/PCAPTool/Private/VCamConfig.cpp`
- `Plugins/PCAPTool/Source/PCAPTool/Public/VCamProcessor.h` — `FPCAPVCamRuntimeState` + `FPCAPVCamProcessor` (pure C++)
- `Plugins/PCAPTool/Source/PCAPTool/Private/VCamProcessor.cpp`
- `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVCamActor.h` — `APCAPVCamActor : ACineCameraActor`
- `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVCamActor.cpp`
- `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVCamSubsystem.h` — `UPCAPVCamSubsystem`
- `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVCamSubsystem.cpp`

**New test file (1):**
- `Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPVCamProcessorTests.cpp`

**Modified (3):**
- `Plugins/PCAPTool/Source/PCAPTool/PCAPTool.Build.cs` — add `CinematicCamera` (Public), `LiveLink` + `LiveLinkInterface` (Private)
- `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPTakeRecorderSubsystem.h` — `AddActorSource` helper + `PendingHasVCam`
- `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTakeRecorderSubsystem.cpp` — arm VCam actor source; set `bHasVCam` on harvest

**Naming:** `VCam` casing (matches `EVCamSystem` / `VCamAsset`); all registered types `PCAP`-prefixed to avoid collision with the engine's native VCam plugin (`UVCamComponent`, `AVCamActor`).

---

## Task 1: Build dependencies

Adds the modules the C++ core needs. Foundational — do first so every later task compiles.

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/PCAPTool.Build.cs`

- [ ] **Step 1: Add `CinematicCamera` to PublicDependencyModuleNames**

In `PCAPTool.Build.cs`, inside the `PublicDependencyModuleNames.AddRange({...})` list, after the `"InputCore",` line add:
```cpp
            "CinematicCamera",      // ACineCameraActor / UCineCameraComponent — APCAPVCamActor base
```

- [ ] **Step 2: Add Live Link modules to PrivateDependencyModuleNames**

In the `PrivateDependencyModuleNames.AddRange({...})` list, after the `"PropertyEditor",` line add:
```cpp
            "LiveLink",             // FLiveLinkClientReference / plugin module
            "LiveLinkInterface",    // ILiveLinkClient, transform role + frame-data types — TPVCam read
```

- [ ] **Step 3: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean (no new code yet — this only links the modules). If `CinematicCamera` / `LiveLinkInterface` are misnamed for 5.7, the build flags it here; fix the module name and rebuild.

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/PCAPTool.Build.cs
git commit -m "build(pcap): add CinematicCamera + LiveLink module deps for VCAM core"
```

---

## Task 2: `UPCAPVCamConfig` DataAsset

The persisted vcam configuration. Mirrors the `UStageConfigAsset` DataAsset precedent. References only `CoreMinimal` types, so it compiles standalone.

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/VCamConfig.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/VCamConfig.cpp`

- [ ] **Step 1: Create `Public/VCamConfig.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCamConfig.generated.h"

// One transform offset (axis correction / zero origin / joystick stacking / saved pose).
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamAlignOffset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FVector  Translation = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FRotator Rotation    = FRotator::ZeroRotator;
};

// Position/rotation smoothing. Speeds are FMath::*InterpTo speeds; 10 is the WVCAM baseline.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamSmoothingConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") bool  bSmoothPosition   = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="1.0", ClampMax="20.0"))
    float PositionSmoothing = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") bool  bSmoothRotation   = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam", meta=(ClampMin="1.0", ClampMax="20.0"))
    float RotationSmoothing = 10.f;
};

// World-space and camera-space scaling of the optical input.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamScaleConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FVector WorldSpaceScale  = FVector(1.f, 1.f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FVector CameraSpaceScale = FVector(1.f, 1.f, 1.f);
};

// Persisted vcam config. One DataAsset per stage setup (saved under the project's
// content; referenced by the active stage). Mirrors UStageConfigAsset.
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPVCamConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FName LiveLinkSubjectName = "TPVCam";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset AlignRigidBody; // axis correction
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset Setup;          // zero origin
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamAlignOffset Navigate;       // joystick stacking

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamSmoothingConfig Smoothing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") FPCAPVCamScaleConfig     Scaling;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<float> FocalLengthPresets = {18.f, 24.f, 35.f, 50.f, 85.f};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") float ActiveFocalLength = 35.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveButtonLayout = 0;   // 0 Default, 1 Var1, 2 Inverted

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") TArray<FPCAPVCamAlignOffset> SavedPositions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam") int32 ActiveSavedPositionIndex = -1;
};
```

- [ ] **Step 2: Create `Private/VCamConfig.cpp`**

```cpp
#include "VCamConfig.h"
```

- [ ] **Step 3: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean; `UPCAPVCamConfig` appears in the editor's **Miscellaneous → Data Asset** picker with all fields editable.

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/VCamConfig.h Plugins/PCAPTool/Source/PCAPTool/Private/VCamConfig.cpp
git commit -m "feat(vcam): UPCAPVCamConfig DataAsset + offset/smoothing/scale structs"
```

---

## Task 3: `FPCAPVCamRuntimeState` + `FPCAPVCamProcessor`

The heart of the system: the WVCAM-equivalent transform chain as pure C++ (no engine context), plus the non-serialized session state it mutates. Locked 14-step order. Offsets compose **left-multiplied** (`Output = Offset * Current`), consistent with the locked `ZeroSpace`/`SetHold` solve.

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/VCamProcessor.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/VCamProcessor.cpp`

- [ ] **Step 1: Create `Public/VCamProcessor.h`**

```cpp
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

private:
    // Step 2 of the chain, also the basis ZeroSpace/SetHold solve against.
    static FTransform ApplyAlign(const UPCAPVCamConfig& Config, const FTransform& Raw);
};
```

- [ ] **Step 2: Create `Private/VCamProcessor.cpp`**

```cpp
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
```

- [ ] **Step 3: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean. (Behavioral verification is Task 4.)

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/VCamProcessor.h Plugins/PCAPTool/Source/PCAPTool/Private/VCamProcessor.cpp
git commit -m "feat(vcam): FPCAPVCamProcessor — locked WVCAM transform chain + ZeroSpace/Hold"
```

---

## Task 4: Processor automation tests (Gate 0)

Pins the locked behaviors with deterministic, single/two-frame tests. Uses fresh runtime state per test so the first-frame smoothing seed gives the computed pose directly (no interp math to reason about). Mirrors `PCAPDataModelTests.cpp`.

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPVCamProcessorTests.cpp`

- [ ] **Step 1: Create the test file**

```cpp
#include "Misc/AutomationTest.h"
#include "VCamConfig.h"
#include "VCamProcessor.h"

#if WITH_DEV_AUTOMATION_TESTS

// Identity: default config, no modes, first frame → output equals the raw input.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamPassthroughTest,
    "PCAP.VCam.Processor.Passthrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamPassthroughTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    FPCAPVCamRuntimeState S;
    const FTransform Raw(FQuat::Identity, FVector(10.f, 20.f, 30.f));

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Raw, 1.f / 60.f);
    TestTrue(TEXT("position passes through on first frame"),
        Out.GetLocation().Equals(FVector(10.f, 20.f, 30.f), 0.01f));
    return true;
}

// Kill Roll: a rolled input comes out with ~zero roll.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamKillRollTest,
    "PCAP.VCam.Processor.KillRoll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamKillRollTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    FPCAPVCamRuntimeState S;
    S.bKillRoll = true;
    const FTransform Raw(FRotator(0.f, 0.f, 30.f).Quaternion(), FVector::ZeroVector);  // 30° roll

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Raw, 1.f / 60.f);
    TestTrue(TEXT("roll zeroed"), FMath::IsNearlyZero(Out.GetRotation().Rotator().Roll, 0.01f));
    return true;
}

// World scale: WorldSpaceScale 2x doubles the input position.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamWorldScaleTest,
    "PCAP.VCam.Processor.WorldScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamWorldScaleTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    C->Scaling.WorldSpaceScale = FVector(2.f, 2.f, 2.f);
    FPCAPVCamRuntimeState S;
    const FTransform Raw(FQuat::Identity, FVector(1.f, 2.f, 3.f));

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Raw, 1.f / 60.f);
    TestTrue(TEXT("position scaled 2x"), Out.GetLocation().Equals(FVector(2.f, 4.f, 6.f), 0.01f));
    return true;
}

// Lock Position: position freezes at the value captured the frame before the lock engaged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamLockPositionTest,
    "PCAP.VCam.Processor.LockPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamLockPositionTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    C->Smoothing.bSmoothPosition = false;   // isolate the lock from interpolation
    FPCAPVCamRuntimeState S;

    // Frame 1: lock off, establishes LockedPosition + primes smoothing at (5,0,0).
    FPCAPVCamProcessor::Process(*C, S, FTransform(FQuat::Identity, FVector(5.f, 0.f, 0.f)), 1.f / 60.f);
    // Frame 2: lock on, input moves to (99,0,0) — output must stay at (5,0,0).
    S.bLockPosition = true;
    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, FTransform(FQuat::Identity, FVector(99.f, 0.f, 0.f)), 1.f / 60.f);

    TestTrue(TEXT("position held"), Out.GetLocation().Equals(FVector(5.f, 0.f, 0.f), 0.01f));
    return true;
}

// ZeroSpace: after zeroing at pose P, processing pose P yields the origin.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamZeroSpaceTest,
    "PCAP.VCam.Processor.ZeroSpace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamZeroSpaceTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    const FTransform P(FRotator(15.f, 40.f, 0.f).Quaternion(), FVector(100.f, -50.f, 25.f));

    FPCAPVCamProcessor::ZeroSpace(*C, P);

    FPCAPVCamRuntimeState S;
    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, P, 1.f / 60.f);
    TestTrue(TEXT("zeroed pose maps to origin (location)"),
        Out.GetLocation().Equals(FVector::ZeroVector, 0.05f));
    TestTrue(TEXT("zeroed pose maps to origin (rotation)"),
        Out.GetRotation().Rotator().IsNearlyZero(0.1f));
    return true;
}

// Hold release: after holding pose P and releasing against a different live pose Q,
// the next processed frame at Q outputs the held pose P.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamHoldReleaseTest,
    "PCAP.VCam.Processor.HoldRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamHoldReleaseTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    C->Smoothing.bSmoothPosition = false;
    C->Smoothing.bSmoothRotation = false;
    FPCAPVCamRuntimeState S;

    const FTransform P(FQuat::Identity, FVector(5.f, 5.f, 5.f));
    const FTransform Q(FQuat::Identity, FVector(50.f, 0.f, 0.f));

    FPCAPVCamProcessor::Process(*C, S, P, 1.f / 60.f);   // prime smoothed at P
    FPCAPVCamProcessor::SetHold(*C, S, true,  P);        // snapshot Held = P
    FPCAPVCamProcessor::SetHold(*C, S, false, Q);        // release against Q → Setup re-solved

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Q, 1.f / 60.f);
    TestTrue(TEXT("output stays at held pose after release"),
        Out.GetLocation().Equals(FVector(5.f, 5.f, 5.f), 0.05f));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: Build + run automation (Madi)**

Build in 5.7.4, then **Tools → Session Frontend → Automation**, filter `PCAP.VCam`, Start Tests. (Or: `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests PCAP.VCam; Quit" -unattended -nopause -testexit="Automation Test Queue Empty"`.)
Expected: all six `PCAP.VCam.Processor.*` tests **PASS**. A failure here means the transform-composition convention needs a fix in `VCamProcessor.cpp` — fix and re-run (do not change the tests to match a wrong result).

- [ ] **Step 3: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPVCamProcessorTests.cpp
git commit -m "test(vcam): automation tests for the transform chain, ZeroSpace, and Hold"
```

---

## Task 5: `APCAPVCamActor`

The single native Cine Camera the subsystem drives and Take Recorder records. Thin — all authority lives in the subsystem.

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVCamActor.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVCamActor.cpp`

- [ ] **Step 1: Create `Public/PCAPVCamActor.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "PCAPVCamActor.generated.h"

// The in-editor virtual camera the VCAM subsystem drives every frame and Take Recorder
// records. One instance, found-or-spawned by UPCAPVCamSubsystem (not per take).
UCLASS()
class PCAPTOOL_API APCAPVCamActor : public ACineCameraActor
{
    GENERATED_BODY()
public:
    APCAPVCamActor();
};
```

- [ ] **Step 2: Create `Private/PCAPVCamActor.cpp`**

```cpp
#include "PCAPVCamActor.h"

APCAPVCamActor::APCAPVCamActor()
{
    // Tag so the subsystem can find an existing instance in the level deterministically.
    Tags.Add(FName(TEXT("PCAP_VCam")));
}
```

- [ ] **Step 3: Build checkpoint (Madi)**

Build in 5.7.4. Expected: compiles clean; `APCAPVCamActor` is placeable from the editor's Place Actors panel (search "PCAPVCam").

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVCamActor.h Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVCamActor.cpp
git commit -m "feat(vcam): APCAPVCamActor (native ACineCameraActor subclass)"
```

---

## Task 6: `UPCAPVCamSubsystem` (Live Link read + tick + API)

Owns the live camera. Reads `TPVCam` from Live Link each editor frame, runs the processor, and drives the camera actor. Exposes the full BlueprintCallable API the deferred panel will bind, plus a status delegate.

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVCamSubsystem.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVCamSubsystem.cpp`

- [ ] **Step 1: Create `Public/PCAPVCamSubsystem.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "TickableEditorObject.h"
#include "VCamProcessor.h"      // FPCAPVCamRuntimeState
#include "PCAPToolTypes.h"      // EStreamStatus
#include "PCAPVCamSubsystem.generated.h"

class UPCAPVCamConfig;
class APCAPVCamActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPCAPVCamStreamStatusChanged, EStreamStatus, NewStatus);

/**
 * Editor-lifetime subsystem owning the live virtual camera.
 * Ticks every editor frame (FTickableEditorObject) while a config + live subject exist:
 * reads TPVCam from Live Link, runs FPCAPVCamProcessor, and drives APCAPVCamActor.
 * Recording is owned by UPCAPTakeRecorderSubsystem, not here.
 *
 * Access: GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>().
 */
UCLASS()
class PCAPTOOL_API UPCAPVCamSubsystem : public UEngineSubsystem, public FTickableEditorObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // FTickableEditorObject
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPCAPVCamSubsystem, STATGROUP_Tickables); }

    // ── Activation ─────────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetActiveConfig(UPCAPVCamConfig* Config);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") UPCAPVCamConfig* GetActiveConfig() const { return ActiveConfig; }

    // The single camera actor (found-or-spawned in the editor world). Used by the record controller.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") APCAPVCamActor* GetOrCreateVCamActor();

    // ── Transform controls ─────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void ZeroSpace();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetHold(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetFlightMode(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetLockPosition(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetLockRotation(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetLockRoll(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetKillRoll(bool bEnabled);

    // ── Navigation / saved positions ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SaveCurrentPosition();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void GotoSavedPosition(int32 Index);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void DeleteSavedPosition(int32 Index);

    // ── Lens ───────────────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetFocalLength(float Millimeters);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void CycleFocalLengthUp();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void CycleFocalLengthDown();

    // ── Gains / scale (joystick seam — stored now, fed by the input layer later) ──
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetTranslationGain(float Gain);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetZoomGain(float Gain);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetWorldSpaceScale(FVector Scale);

    // ── Readouts ───────────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") EStreamStatus GetStreamStatus() const { return StreamStatus; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") FTransform GetCurrentTransform() const;
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") float GetCurrentFocalLength() const;
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") FString GetActiveMapping() const { return RuntimeState.ActiveMapping; }

    UPROPERTY(BlueprintAssignable, Category="PCAP|VCam") FOnPCAPVCamStreamStatusChanged OnStreamStatusChanged;

private:
    UPROPERTY() TObjectPtr<UPCAPVCamConfig> ActiveConfig = nullptr;
    UPROPERTY() TWeakObjectPtr<APCAPVCamActor> VCamActor;

    FPCAPVCamRuntimeState RuntimeState;
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;

    // Reads the active config's Live Link subject transform. Returns false if unavailable.
    bool ReadLiveLinkTransform(FTransform& OutTransform) const;
    void SetStreamStatus(EStreamStatus NewStatus);
};
```

- [ ] **Step 2: Create `Private/PCAPVCamSubsystem.cpp`**

```cpp
#include "PCAPVCamSubsystem.h"
#include "VCamConfig.h"
#include "PCAPVCamActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"            // TActorIterator
#include "Editor.h"                 // GEditor
#include "CineCameraComponent.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

// ── Lifecycle ───────────────────────────────────────────────────────────────

void UPCAPVCamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UPCAPVCamSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

// ── Tick ────────────────────────────────────────────────────────────────────

bool UPCAPVCamSubsystem::IsTickable() const
{
    // Idle cheaply until a config is assigned (the panel/record controller sets it).
    return ActiveConfig != nullptr;
}

void UPCAPVCamSubsystem::Tick(float DeltaTime)
{
    if (!ActiveConfig) { return; }

    FTransform Raw;
    if (!ReadLiveLinkTransform(Raw))
    {
        SetStreamStatus(EStreamStatus::Disconnected);
        return;
    }
    SetStreamStatus(EStreamStatus::Connected);

    const FTransform Out = FPCAPVCamProcessor::Process(*ActiveConfig, RuntimeState, Raw, DeltaTime);

    if (APCAPVCamActor* Cam = GetOrCreateVCamActor())
    {
        Cam->SetActorTransform(Out);
        if (UCineCameraComponent* CC = Cam->GetCineCameraComponent())
        {
            CC->SetCurrentFocalLength(FPCAPVCamProcessor::GetFocalLength(*ActiveConfig));
        }
    }
}

// ── Live Link read ──────────────────────────────────────────────────────────
// CONFIRM-AT-BUILD: the transform role + frame-data accessor are the 5.7-vs-5.4 risk
// point (the rest of the chain is engine-stable). Log if the subject can't be evaluated.

bool UPCAPVCamSubsystem::ReadLiveLinkTransform(FTransform& OutTransform) const
{
    if (!ActiveConfig) { return false; }

    IModularFeatures& MF = IModularFeatures::Get();
    if (!MF.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)) { return false; }

    ILiveLinkClient& Client = MF.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

    FLiveLinkSubjectFrameData FrameData;
    const bool bOk = Client.EvaluateFrame_AnyThread(
        ActiveConfig->LiveLinkSubjectName,
        ULiveLinkTransformRole::StaticClass(),
        FrameData);
    if (!bOk) { return false; }

    if (const FLiveLinkTransformFrameData* T = FrameData.FrameData.Cast<FLiveLinkTransformFrameData>())
    {
        OutTransform = T->Transform;
        return true;
    }
    return false;
}

void UPCAPVCamSubsystem::SetStreamStatus(EStreamStatus NewStatus)
{
    if (StreamStatus == NewStatus) { return; }
    StreamStatus = NewStatus;
    OnStreamStatusChanged.Broadcast(NewStatus);
}

// ── Activation / actor ──────────────────────────────────────────────────────

void UPCAPVCamSubsystem::SetActiveConfig(UPCAPVCamConfig* Config)
{
    ActiveConfig = Config;
    RuntimeState = FPCAPVCamRuntimeState();   // reset session state on (re)activation
}

APCAPVCamActor* UPCAPVCamSubsystem::GetOrCreateVCamActor()
{
    if (VCamActor.IsValid()) { return VCamActor.Get(); }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) { return nullptr; }

    for (TActorIterator<APCAPVCamActor> It(World); It; ++It)
    {
        VCamActor = *It;
        return VCamActor.Get();
    }

    APCAPVCamActor* Spawned = World->SpawnActor<APCAPVCamActor>();
    VCamActor = Spawned;
    return Spawned;
}

// ── Transform controls ──────────────────────────────────────────────────────

void UPCAPVCamSubsystem::ZeroSpace()
{
    if (!ActiveConfig) { return; }
    FTransform Raw;
    if (ReadLiveLinkTransform(Raw)) { FPCAPVCamProcessor::ZeroSpace(*ActiveConfig, Raw); }
}

void UPCAPVCamSubsystem::SetHold(bool bEnabled)
{
    if (!ActiveConfig) { return; }
    FTransform Raw;
    ReadLiveLinkTransform(Raw);   // raw needed only on release; safe if it fails
    FPCAPVCamProcessor::SetHold(*ActiveConfig, RuntimeState, bEnabled, Raw);
}

void UPCAPVCamSubsystem::SetFlightMode(bool bEnabled)   { RuntimeState.bFlightMode   = bEnabled; }
void UPCAPVCamSubsystem::SetLockPosition(bool bEnabled) { RuntimeState.bLockPosition = bEnabled; }
void UPCAPVCamSubsystem::SetLockRotation(bool bEnabled) { RuntimeState.bLockRotation = bEnabled; }
void UPCAPVCamSubsystem::SetLockRoll(bool bEnabled)     { RuntimeState.bLockRoll     = bEnabled; }
void UPCAPVCamSubsystem::SetKillRoll(bool bEnabled)     { RuntimeState.bKillRoll     = bEnabled; }

// ── Navigation / saved positions ────────────────────────────────────────────

void UPCAPVCamSubsystem::SaveCurrentPosition()
{
    if (!ActiveConfig) { return; }
    ActiveConfig->SavedPositions.Add(ActiveConfig->Navigate);
    ActiveConfig->ActiveSavedPositionIndex = ActiveConfig->SavedPositions.Num() - 1;
}

void UPCAPVCamSubsystem::GotoSavedPosition(int32 Index)
{
    if (!ActiveConfig) { return; }
    if (ActiveConfig->SavedPositions.IsValidIndex(Index))
    {
        ActiveConfig->Navigate = ActiveConfig->SavedPositions[Index];
        ActiveConfig->ActiveSavedPositionIndex = Index;
    }
}

void UPCAPVCamSubsystem::DeleteSavedPosition(int32 Index)
{
    if (!ActiveConfig) { return; }
    if (ActiveConfig->SavedPositions.IsValidIndex(Index))
    {
        ActiveConfig->SavedPositions.RemoveAt(Index);
        ActiveConfig->ActiveSavedPositionIndex = -1;
    }
}

// ── Lens ────────────────────────────────────────────────────────────────────

void UPCAPVCamSubsystem::SetFocalLength(float Millimeters)
{
    if (ActiveConfig) { ActiveConfig->ActiveFocalLength = Millimeters; }
}

void UPCAPVCamSubsystem::CycleFocalLengthUp()
{
    if (!ActiveConfig || ActiveConfig->FocalLengthPresets.Num() == 0) { return; }
    int32 Best = INDEX_NONE;
    for (int32 i = 0; i < ActiveConfig->FocalLengthPresets.Num(); ++i)
    {
        if (ActiveConfig->FocalLengthPresets[i] > ActiveConfig->ActiveFocalLength + KINDA_SMALL_NUMBER)
        {
            Best = i; break;   // presets are authored ascending
        }
    }
    if (Best != INDEX_NONE) { ActiveConfig->ActiveFocalLength = ActiveConfig->FocalLengthPresets[Best]; }
}

void UPCAPVCamSubsystem::CycleFocalLengthDown()
{
    if (!ActiveConfig || ActiveConfig->FocalLengthPresets.Num() == 0) { return; }
    int32 Best = INDEX_NONE;
    for (int32 i = ActiveConfig->FocalLengthPresets.Num() - 1; i >= 0; --i)
    {
        if (ActiveConfig->FocalLengthPresets[i] < ActiveConfig->ActiveFocalLength - KINDA_SMALL_NUMBER)
        {
            Best = i; break;
        }
    }
    if (Best != INDEX_NONE) { ActiveConfig->ActiveFocalLength = ActiveConfig->FocalLengthPresets[Best]; }
}

// ── Gains / scale (stored; consumed by the deferred input layer) ─────────────

void UPCAPVCamSubsystem::SetTranslationGain(float Gain) { RuntimeState.TranslationGain = FMath::Clamp(Gain, 0.01f, 1.0f); }
void UPCAPVCamSubsystem::SetZoomGain(float Gain)        { RuntimeState.ZoomGain        = FMath::Clamp(Gain, 0.01f, 1.0f); }
void UPCAPVCamSubsystem::SetWorldSpaceScale(FVector Scale)
{
    if (ActiveConfig) { ActiveConfig->Scaling.WorldSpaceScale = Scale; }
}

// ── Readouts ────────────────────────────────────────────────────────────────

FTransform UPCAPVCamSubsystem::GetCurrentTransform() const
{
    return FTransform(RuntimeState.SmoothedRotation, RuntimeState.SmoothedPosition);
}

float UPCAPVCamSubsystem::GetCurrentFocalLength() const
{
    return ActiveConfig ? ActiveConfig->ActiveFocalLength : 0.f;
}
```

- [ ] **Step 3: Build + live smoke (Madi) — Gate 2**

Build in 5.7.4. Then: create a `UPCAPVCamConfig` asset, call `SetActiveConfig` (Blueprint or a temporary console hook), and stream a `TPVCam` (or any transform) Live Link subject. Expected: an `APCAPVCamActor` appears/updates, tracking the subject; `ZeroSpace`/`SetHold`/locks behave; `GetStreamStatus` returns Connected while streaming, Disconnected when stopped. If `EvaluateFrame_AnyThread` / `FLiveLinkTransformFrameData` don't resolve on 5.7, fix the accessor in `ReadLiveLinkTransform` and rebuild.

- [ ] **Step 4: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVCamSubsystem.h Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVCamSubsystem.cpp
git commit -m "feat(vcam): UPCAPVCamSubsystem — Live Link read, editor tick, camera drive + API"
```

---

## Task 7: Take Recorder hookup (arm VCam source + flag the take)

Finishes the VCam slice the Phase 2 controller designed (`PCAPTakeRecorderSubsystem.cpp:144` — "VCam … arming are the next reflection pass"). When the active stage records a vcam, the camera actor is armed as a Take Recorder actor source so its motion records into the take; the harvested `FTake` is flagged `bHasVCam`.

**Files:**
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPTakeRecorderSubsystem.h`
- Modify: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTakeRecorderSubsystem.cpp`

- [ ] **Step 1: Declare the actor-source helpers + pending flag in the header**

In `PCAPTakeRecorderSubsystem.h`, in the private section after the existing `bool AddLiveLinkSource(UObject* Sources, FName SubjectName) const;` declaration, add:
```cpp
    // Arms an actor recording source for Target (used for the VCam camera). Resolves
    // /Script/TakeRecorderSources.TakeRecorderActorSource and sets its Target via FProperty.
    bool AddActorSource(UObject* Sources, AActor* Target) const;
    static void SetSourceLazyActor(UObject* Source, const TCHAR* PropName, AActor* Value);
```
And in the private state, after the existing `FString PendingShotID;` line, add:
```cpp
    bool PendingHasVCam = false;   // a VCam source was armed for the in-flight take
```

- [ ] **Step 2: Add includes to the cpp**

In `PCAPTakeRecorderSubsystem.cpp`, after the existing `#include "PCAPToolPaths.h"` line, add:
```cpp
#include "StageConfigAsset.h"
#include "PCAPVCamSubsystem.h"
#include "PCAPVCamActor.h"
#include "UObject/LazyObjectPtr.h"
```

- [ ] **Step 3: Arm the VCam source in `StartRecordForActiveShot`**

In `StartRecordForActiveShot`, immediately after the body/face arming loop closes (the `for (const FShotSubject& Subj : Shot->Subjects)` block that calls `AddLiveLinkSource`, ending just before the `// Slate = ShotID` comment), insert:
```cpp
    // Arm the VCam camera actor as a source if the active stage records a vcam.
    PendingHasVCam = false;
    if (UStageConfigAsset* Stage = DB->GetActiveStageConfig())
    {
        if (Stage->VCamSystem != EVCamSystem::None)
        {
            if (UPCAPVCamSubsystem* VCamSys = GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>())
            {
                if (AActor* Cam = VCamSys->GetOrCreateVCamActor())
                {
                    PendingHasVCam = AddActorSource(Sources, Cam);
                }
            }
        }
    }
```

- [ ] **Step 4: Flag the take in `HandleTakeFinished`**

In `HandleTakeFinished`, immediately after the existing `Take.MasterSequence = SequenceAsset;` line, add:
```cpp
    Take.bHasVCam = PendingHasVCam;   // VCamAsset (the dedicated sub-asset) resolves in the
                                      // same later per-stream pass as Body/Face/Audio refs.
```

- [ ] **Step 5: Implement the actor-source helpers at the end of the cpp**

Append after the existing `AddLiveLinkSource` definition:
```cpp
// ── Reflection: arm an engine-private Actor source (VCam camera) ─────────────
// CONFIRM-AT-BUILD: class path + "Target" property name/type (TLazyObjectPtr<AActor>
// → FLazyObjectProperty) are the 5.7 risk points — log if either lookup is null.

void UPCAPTakeRecorderSubsystem::SetSourceLazyActor(UObject* Source, const TCHAR* PropName, AActor* Value)
{
    if (!Source) { return; }
    if (FLazyObjectProperty* Prop = FindFProperty<FLazyObjectProperty>(Source->GetClass(), PropName))
    {
        FLazyObjectPtr Lazy;
        Lazy = Value;
        Prop->SetPropertyValue_InContainer(Source, Lazy);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Actor source has no '%s' FLazyObjectProperty — confirm against 5.7 source."), PropName);
    }
}

bool UPCAPTakeRecorderSubsystem::AddActorSource(UObject* Sources, AActor* Target) const
{
    UTakeRecorderSources* Src = Cast<UTakeRecorderSources>(Sources);
    if (!Src || !Target) { return false; }

    UClass* ActorSourceClass = FindSourceClass(TEXT("/Script/TakeRecorderSources.TakeRecorderActorSource"));
    if (!ActorSourceClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] TakeRecorderActorSource class not found — confirm /Script path on 5.7."));
        return false;
    }

    UTakeRecorderSource* NewSrc = Src->AddSource(ActorSourceClass);
    SetSourceLazyActor(NewSrc, TEXT("Target"), Target);
    return NewSrc != nullptr;
}
```

- [ ] **Step 6: Build + record smoke (Madi) — Gate 3**

Build in 5.7.4. With a stage whose `VCamSystem = Technoprops` and a streaming `TPVCam`, press RECORD on the active shot. Expected: the take records to the `…/Productions/…/<TakeID>/` folder; the resulting `FTake` on the shot has `bHasVCam == true` and the camera's motion is present in the recorded master sequence. If `/Script/TakeRecorderSources.TakeRecorderActorSource` or the `Target` property don't resolve, the warning logs point to the fix.

- [ ] **Step 7: Commit**

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPTakeRecorderSubsystem.h Plugins/PCAPTool/Source/PCAPTool/Private/PCAPTakeRecorderSubsystem.cpp
git commit -m "feat(vcam): arm VCam camera as a Take Recorder source; flag take bHasVCam"
```

---

## Self-Review

**1. Spec coverage** — every in-scope §1 item maps to a task:
- Build deps (§9, V-D7) → Task 1
- `UPCAPVCamConfig` + structs (§4) → Task 2
- `FPCAPVCamRuntimeState` + `FPCAPVCamProcessor` 14-step chain / ZeroSpace / SetHold (§5) → Task 3; tests (§11 Gate 0) → Task 4
- `APCAPVCamActor` (§7, V-D3) → Task 5
- `UPCAPVCamSubsystem` + tick + Live Link read + API (§6, V-D2) → Task 6
- Take Recorder hookup (§8, V-D1) → Task 7
- Deferred (panel, joystick, session status — §10, V-D6) → **not in this plan, by design**; the subsystem API + `FPCAPVCamRuntimeState` joystick fields are the seams.

**2. Placeholder scan** — no TBD/TODO. Every code step has complete code. "CONFIRM-AT-BUILD" markers are concrete, log-on-null instructions on specific lines (the established house stance for reflection/Live Link API), not deferred work.

**3. Type consistency** — checked across tasks:
- `UPCAPVCamConfig` fields (`LiveLinkSubjectName`, `AlignRigidBody`, `Setup`, `Navigate`, `Scaling.WorldSpaceScale`, `Smoothing.*`, `FocalLengthPresets`, `ActiveFocalLength`, `SavedPositions`, `ActiveSavedPositionIndex`) — defined Task 2, used identically in Tasks 3 & 6.
- `FPCAPVCamProcessor::Process/ZeroSpace/SetHold/GetFocalLength` signatures — defined Task 3, called identically in Tasks 4 & 6.
- `FPCAPVCamRuntimeState` members — defined Task 3, mutated in Task 6 setters.
- `UPCAPVCamSubsystem::GetOrCreateVCamActor()` returns `APCAPVCamActor*` — defined Task 6, called in Task 7 (assigned to `AActor*`, valid upcast).
- `AddActorSource(UObject*, AActor*)` / `SetSourceLazyActor(UObject*, const TCHAR*, AActor*)` / `PendingHasVCam` — declared Task 7 Step 1, defined/used Task 7 Steps 3-5. `Take.bHasVCam` is an existing `FTake` field (PCAPToolTypes.h:725).

**Behavioral note:** the processor's left-multiply composition is self-consistent and pinned by Task 4's tests. It is validated against *real WVCAM behavior* only at the live smoke gate (Task 6) — a mismatch there is a convention fix in `VCamProcessor.cpp`, caught before the panel is ever built.

---

## Notes for the executor
- **No local build.** Every "build checkpoint" / "smoke" runs on Madi's 5.7.4 machine. Do not mark a task complete until she confirms.
- **Order matters.** Task 1 (deps) must land first; Task 7 depends on Tasks 5 & 6 (the actor + `GetOrCreateVCamActor`).
- **Stop-and-fix on red.** If a build fails, read the error log, fix in place, request a rebuild — never push past a red build.
- **Three confirm-at-build points** (all log on null): Live Link frame accessor (Task 6), `TakeRecorderActorSource` class path + `Target` property (Task 7). Same risk class as the existing `AddLiveLinkSource`.
- **Fetch before push.** `git fetch` and confirm `origin/main` is an ancestor of HEAD before any push (parallel-session rule).
