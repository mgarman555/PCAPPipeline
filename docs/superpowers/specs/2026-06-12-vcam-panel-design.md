# PCAP Tool — VCAM Operator Panel: C++ Core (Design)

**Date:** 2026-06-12
**Status:** Design — approved in brainstorming; build pending (Madi compiles on Windows / UE 5.7.4)
**Engine:** UE 5.7.4 (API to be verified against local UE 5.4 source — Live Link / CineCamera / Take Recorder APIs are stable across 5.x; flag any 5.7 deltas at build)
**Depends on:** Phase 1 (data model) + Phase 2 (Take Recorder controller). This is additive — it does **not** modify existing panels, the data hierarchy, or session management.
**Source spec:** the "PCAPTool — VCAM Panel: Architecture & Claude Code Handoff" handoff (2026-06-12). This document reconciles that handoff against the real plugin and supersedes it where they conflict.

---

## 1. Scope

VCAM is the virtual-camera operator surface that replaces the WVCAM application as the interactive tool. It reads the tracked camera rig from Live Link (`TPVCam`), applies the WVCAM-equivalent transform processing, drives a Cine Camera inside Unreal, and lets the existing record controller capture that camera into each take. WVCAM is reduced to a silent HID shim (unchanged, out of scope).

**This session — the C++ core (compile-safe foundation):**
1. `UPCAPVCamConfig` (DataAsset) + its offset/smoothing/scale structs.
2. `FPCAPVCamRuntimeState` (non-serialized session state).
3. `FPCAPVCamProcessor` — the full WVCAM transform chain (pure C++, unit-tested).
4. `UPCAPVCamSubsystem` — Live Link read, per-frame tick, drives the camera, exposes the BlueprintCallable API + delegates the future panel will bind.
5. `APCAPVCamActor` — native `ACineCameraActor` subclass; one instance, found-or-spawned.
6. **Take Recorder hookup** — finish the VCam arming + `VCamAsset` harvest the Phase 2 controller already designed.
7. `Build.cs` module additions (`CinematicCamera`, `LiveLink`, `LiveLinkInterface`).
8. Processor unit tests (`Private/Tests/`).

**Deferred to follow-up sessions (seams defined here):**
- **Joystick input layer** (`FVCamInputLayer`, the three button layouts, gain math). *Open contradiction to resolve first — see §10.*
- **`SPCAPVCamPanel`** Slate panel + Nomad tab under "PCAP Tools".
- **Session stream-status** reporting (a `FVCamStreamEntry` read by the Realtime Operator panel, which lives on another branch).

---

## 2. Handoff corrections (reconciled against the real plugin)

The handoff is sound on the C++ data model but assumes a Blueprint/Content-based plugin. The real PCAPTool is pure-C++ Slate with `"CanContainContent": false`. Corrections:

| Handoff said | Reality | Correction |
|---|---|---|
| `WBP_VCAMPanel.uasset` + sub-widgets (Editor Utility Widgets) in `Content/VCAM/` | Every operator surface is a C++ `SWidget` Nomad tab (`SPCAPOperatorConsole`, `SHMCSetupPanel`…); plugin can hold **no** content | Panel becomes `SPCAPVCamPanel` (Slate), a tab under the "PCAP Tools" workspace group. *(Deferred this session.)* |
| `BP_PCAP_VCamActor.uasset` (Blueprint subclass) placed in level | Plugin can't store assets | Native **`APCAPVCamActor : ACineCameraActor`** in code; subsystem finds-or-spawns one instance. |
| `UPCAPVCAMSubsystem : UEditorSubsystem` | Siblings (`UPCAPToolSubsystem`, `UPCAPTakeRecorderSubsystem`) are `UEngineSubsystem` | `UPCAPVCamSubsystem : public UEngineSubsystem` for consistency; accessed via `GEngine->GetEngineSubsystem<>()`. |
| "called every tick by the subsystem" | Engine subsystems have no world/tick (HMC polls via a 3 s `GEditor` timer — too slow for a camera) | Subsystem also derives **`FTickableEditorObject`** for a real per-frame loop; `IsTickable()` gates on active config + live subject. |
| VCAM registers its own Take Recorder source + writes `FTake.VCamAsset` | A central controller (`UPCAPTakeRecorderSubsystem`) owns the record flow and **already designed** VCam arming + `VCamAsset` harvest (Phase 2 doc §4, lines 58 & 63) | VCAM owns the **live** camera only; the existing controller **records** it. One master clock, one harvest path. |
| Output to `Content/Mocap/[DayID]/[ShotID]/[TakeID]/VCAM/…` | Paths are centralized in `PCAPPaths::` ("never hardcode `/Game/...`"); takes go under `Productions()` | Reuse the controller's existing take-path logic; no new path code. `PipelineTools/VCam` is reserved for any tool-owned assets (none needed this session). |
| Types named `UVCAMConfig` / `FVCAM*` | Data model uses **`VCam`** casing (`EVCamSystem`, `VCamAsset`, `bHasVCam`); UE's native **VCam plugin is in the stack** (`UVCamComponent`, `AVCamActor`) → UObject names must be globally unique | Use `VCam` casing **and** a `PCAP`/`PCAPVCam` prefix on all registered types to avoid collision with the engine VCam plugin. |

**Unchanged from the handoff (locked — do not deviate):** the 14-step transform chain *order*; `ZeroSpace` / `Hold` semantics; config persists as a DataAsset; one placed camera actor (not per-take); `FTake.VCamAsset` follows the existing take naming.

---

## 3. Decisions

| # | Decision | Source |
|---|---|---|
| V-D1 | **Live vs recorded split.** VCAM subsystem drives the live camera; `UPCAPTakeRecorderSubsystem` arms it as a source and harvests `VCamAsset`. VCAM never calls Take Recorder itself. | Reconciliation (matches Phase 2 §4) |
| V-D2 | **Subsystem = `UEngineSubsystem` + `FTickableEditorObject`** (not the handoff's `UEditorSubsystem`). | Consistency w/ existing subsystems |
| V-D3 | **Native `APCAPVCamActor`**, found-or-spawned as a single instance; no BP asset, no `Content/`. | `CanContainContent:false` |
| V-D4 | **`PCAP`-prefixed `VCam` type names** to avoid collision with the engine VCam plugin. | UObject global uniqueness |
| V-D5 | **Transform processor is plain C++** (no UObject), unit-tested — the one piece fully verifiable without hardware. | Risk reduction (no local compile) |
| V-D6 | **Joystick layer + panel deferred**; runtime state holds the joystick fields now, driven via the BlueprintCallable API until the input layer lands, so the processing chain stays complete. | Madi (session scope) |
| V-D7 | **`Build.cs` gains** `CinematicCamera` (Public), `LiveLink` + `LiveLinkInterface` (Private). | Missing deps for CineCamera + Live Link read |

---

## 4. Data model

New header `VCamConfig.h` (DataAsset + persisted structs). Mirrors the `UStageConfigAsset` DataAsset precedent.

```cpp
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamAlignOffset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector  Translation = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator Rotation    = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamSmoothingConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bSmoothPosition   = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PositionSmoothing = 10.f;  // 1–20, 10 baseline
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bSmoothRotation   = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RotationSmoothing = 10.f;
};

USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPVCamScaleConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector WorldSpaceScale  = FVector(1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector CameraSpaceScale = FVector(1.f);
};

UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPVCamConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName LiveLinkSubjectName = "TPVCam";
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FPCAPVCamAlignOffset AlignRigidBody; // axis correction
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FPCAPVCamAlignOffset Setup;          // zero origin
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FPCAPVCamAlignOffset Navigate;       // joystick stacking
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FPCAPVCamSmoothingConfig Smoothing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FPCAPVCamScaleConfig     Scaling;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> FocalLengthPresets = {18.f,24.f,35.f,50.f,85.f};
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ActiveFocalLength = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ActiveButtonLayout = 0;        // 0 Default,1 Var1,2 Inverted
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FPCAPVCamAlignOffset> SavedPositions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ActiveSavedPositionIndex = -1;
};
```

`FPCAPVCamRuntimeState` — **plain C++ struct** (no UHT), lives in the subsystem, resets per session. Holds: mode flags (`bFlightMode`, `bLockPosition/Rotation/Roll`, `bKillRoll`, `bHold`), the smoothed output (`SmoothedPosition`/`SmoothedRotation`), the hold snapshot (`HeldPosition`/`HeldRotation`), last-known values for the locks, and the joystick fields (gains, multipliers, mapping state, Sony XY accumulator). The joystick subset is **inert this session** — set via API, zero until the input layer lands.

---

## 5. Transform processor — `FPCAPVCamProcessor`

Plain C++ class (no UObject). Inputs: `const UPCAPVCamConfig&`, `FPCAPVCamRuntimeState&`, raw Live Link transform, `DeltaTime`. Outputs: final `FTransform` + focal length. **The chain order is locked (do not reorder):**

```
1.  Read raw transform from Live Link (TPVCam)
2.  Apply AlignRigidBody offset (axis correction)
3.  Kill Roll (zero roll from source) if enabled
4.  Lock Roll (freeze roll at last value) if enabled
5.  World-space scaling
6.  Hold → use held transform instead of live
7.  Lock Position → last known position, live rotation
8.  Lock Rotation → live position, last known rotation
9.  Apply Setup offset (zero origin)
10. Apply Navigate offset (joystick stacking)
11. Smoothing (position & rotation independent, configurable)
12. Flight Mode → joystick axes relative to camera orientation; else relative to world
13. Output FTransform to the camera actor
14. Output focal length to the camera actor
```

`ZeroSpace()` and `SetHold(bool)` carry the exact handoff semantics (Setup re-solved so the current optical pose becomes origin / matches the held pose; Navigate cleared on zero). Hold-release deliberately reproduces WVCAM's Setup repopulation (and its known roll artifact). Smoothing uses `DeltaTime` (frame-rate-independent interp), not a fixed per-tick constant.

**Why pure C++:** no engine context needed → fully unit-testable (see §11), which is the highest-value safety net given I can't compile locally.

---

## 6. Subsystem + tick — `UPCAPVCamSubsystem`

`class UPCAPVCamSubsystem : public UEngineSubsystem, public FTickableEditorObject`.

- **Owns:** active `UPCAPVCamConfig*`, `FPCAPVCamRuntimeState`, an `FPCAPVCamProcessor`, and a weak ref to the `APCAPVCamActor`.
- **Live Link read:** `ILiveLinkClient` via `IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(...)`; `EvaluateFrame_AnyThread` for `LiveLinkSubjectName` with the Transform (or Camera) role; read the transform from the frame data. *(Exact 5.7 frame-data accessors verified against 5.4 source at build.)*
- **Tick** (`FTickableEditorObject::Tick`): read TPVCam → `Processor.Process(...)` → `VCamActor->SetActorTransform(Out)` + `GetCineCameraComponent()->SetCurrentFocalLength(...)`. `IsTickable()` returns true only with an active config and a resolvable subject (so it idles cheaply when unused).
- **BlueprintCallable API** (full handoff list): `ZeroSpace`, `SetHold`, `SetFlightMode`, `SetLock{Position,Rotation,Roll}`, `SetKillRoll`, `SetActiveButtonLayout`, `SaveCurrentPosition`, `GotoSavedPosition`, `DeleteSavedPosition`, `SetFocalLength`, `CycleFocalLength{Up,Down}`, `SetTranslationGain`, `SetZoomGain`, `SetWorldSpaceScale`, `GetStreamStatus`→`EStreamStatus`, `GetCurrentTransform`, `GetCurrentFocalLength`, `GetActiveMapping`.
- **Delegates** (`BlueprintAssignable`, mirroring the HMC subsystem pattern) for the deferred panel: e.g. `OnVCamStatusChanged`, `OnTransformUpdated` — so building `SPCAPVCamPanel` later needs **zero** subsystem changes.

---

## 7. Camera actor — `APCAPVCamActor`

`class APCAPVCamActor : public ACineCameraActor`. Native, no asset. The subsystem **finds an existing instance in the editor world, or spawns one** on activation, and keeps a single instance alive (handoff: "single actor placed in the level, not spawned per take"). The actor itself is thin — all transform/lens authority lives in the subsystem; the actor is the thing Take Recorder records.

---

## 8. Take Recorder integration (finish what Phase 2 designed)

Phase 2 (§4) already plans: *Prepare* adds "actor source for VCam"; *Harvest* resolves `VCamAsset`. This session implements that VCam-specific slice:

- **Prepare:** when the active stage's `VCamSystem != EVCamSystem::None`, the controller arms an actor source targeting the `APCAPVCamActor`. Mirrors the existing `AddLiveLinkSource` reflection helper with an `AddActorSource` helper resolving `/Script/TakeRecorderSources.TakeRecorderActorSource` and setting its target via `FProperty`. *(Class path + target property name confirmed at build — same risk class as the existing source arming.)*
- **Harvest** (`HandleTakeFinished`): set `FTake.VCamAsset` and `bHasVCam = true`, resolving the recorded camera Level Sequence with the **same per-stream path logic Phase 2 already uses** for `BodyAnimAssets`/`FaceAnimAssets`/`AudioAssets` (`BuildTakeAssetPath` under `PCAPPaths::Productions()`). Whether Take Recorder emits the camera as its own sub-asset or as a possessable in the master sequence is confirmed at build — no new path code either way.
- VCAM subsystem exposes `APCAPVCamActor* GetVCamActor()` for the controller to arm.

This keeps one master clock and guarantees the camera lands in the same take, folder, and timecode as body/face/audio.

---

## 9. Build dependencies

`PCAPTool.Build.cs`:
- **Public:** add `"CinematicCamera"` (for `ACineCameraActor` / `UCineCameraComponent` in the public actor header).
- **Private:** add `"LiveLink"` and `"LiveLinkInterface"` (for `ILiveLinkClient` + frame-data types in the subsystem `.cpp`).

`TakeRecorder` / `TakesCore` / `LevelSequence` / `MovieScene` are already linked (Phase 2). The LiveLink **plugin** is already enabled in `.uplugin`; these are the missing **module** deps. Module names are stable across 5.x.

---

## 10. Deferred work + open questions

- **Joystick input layer (`FVCamInputLayer`)** — the three button layouts (Default / Variation 1 / Inverted Default), gain math (translation 0.01–1.0, `dGain = dCount/16384`, deadband 100, zoom precision ×10 / ÷200, 500-unit increments, `GAIN_m_TO_cm = 100`, 0.5 s sticky, 1 s HUD refresh, 2 s hold), saved-position cycling, Sony XY accumulator.
  **Open contradiction (resolve before building this):** the handoff's "What Is NOT Being Built" says *don't read WVCAM's UDP broadcast*, but the Joystick Input Layer says *read joystick state from WVCAM's broadcast output*. Decision needed: read the WVCAM broadcast (a UDP receiver — contradicts the stated non-goal) vs. wait for direct HID. The runtime state + API seam built this session make either a drop-in.
- **`SPCAPVCamPanel` + Nomad tab** — five sections (stream status, transform controls, navigation/saved positions, lens, controller), bound to §6's API + delegates. Registered under the "PCAP Tools" group like Operator Console / HMC Monitor.
- **Session stream status** — add a lightweight `FVCamStreamEntry` (additive) the Realtime Operator reads like body/face/audio. That panel is on a separate branch (`claude/gallant-cerf-fe61d0`); coordinate the fold-in there.

---

## 11. Verification

- **Gate 0 — Unit tests (here, no engine):** `FPCAPVCamProcessor` chain — AlignRigidBody, Kill/Lock Roll, scaling, Hold, Lock Position/Rotation, Setup, Navigate, smoothing convergence, Flight-mode axis basis, `ZeroSpace`, `SetHold` release. Precedent: `Private/Tests/PCAPDataModelTests.cpp`.
- **Gate 1 — Build (Madi, 5.7.4):** compiles with the new module deps; confirm Live Link frame-data accessors and `/Script/TakeRecorderSources.TakeRecorderActorSource` resolve (log null `FindObject<UClass>` / missing `FProperty`).
- **Gate 2 — Live smoke:** with `TPVCam` streaming, the `APCAPVCamActor` tracks the rig; `ZeroSpace`/`Hold`/locks/smoothing behave; focal-length set/cycle works; `GetStreamStatus` reflects connection.
- **Gate 3 — Record smoke:** with `VCamSystem = Technoprops` on the active stage, RECORD produces a take whose `FTake.VCamAsset` is set and `bHasVCam = true`, in the same `Productions/...` folder as the body/face/audio of that take.

---

## 12. Risks

- **No local compile** — mitigated by pure-C++ unit-tested processor (Gate 0) and the established "build-loop verify" stance for reflection/engine-private API.
- **Live Link 5.7 read API drift** — verified against 5.4; 5.7 deltas surface at build (Gate 1). Isolate the read in one function.
- **Take Recorder actor-source reflection** — same fragile class as the existing Live Link source arming; `/Script` path + target property confirmed at build; log null lookups.
- **Editor tick without PIE** — `FTickableEditorObject` ticks in-editor; confirm it fires for the editor-world camera (HMC hit a related "world timer doesn't fire outside PIE" issue and worked around it via a Slate timer — fallback available if needed).
- **Engine VCam plugin name collisions** — avoided by the `PCAP`/`PCAPVCam` prefix on all registered types (V-D4).
- **Parallel-session git races** — fetch + check `origin/main` is an ancestor of HEAD before any build/push (project rule).
