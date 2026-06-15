# Volume Visualizer — design spec

**Date:** 2026-06-15
**Status:** design — pending Madi's review (not yet built)
**Author:** Madi + Claude (brainstormed 2026-06-15)

## Goal

See everything Vicon/Shogun is tracking, **as marker dots, in the UE viewport, to scale inside the stage FBX.** One color per labeled prop/subject; a neutral color for unlabeled markers. This is the spatial companion to the Floor Monitor (which is a read-only *text* list of subjects) — same data, projected into 3D space.

No skeleton bones, no labels-as-text, no custom embedded viewport. Just dots in the level viewport.

## Scope

**In:**
- A placeable actor that renders tracked points as instanced-mesh dots, colored per subject.
- Two data sources behind one interface:
  - **Phase 1 — Live Link stand-in** (no new dependency): a dot at each labeled prop's tracked position and at each subject's joints. These are *solved* points (joint centers), not the physical markers — a stand-in to build and prove the pipeline.
  - **Phase 2 — Vicon DataStream SDK** (needs the SDK vendored in): the real marker cloud — unlabeled markers + per-subject labeled markers, exactly what the capture operator sees.
- Runtime alignment knobs (Unit Scale, Origin Offset, Yaw) so dots snap onto the FBX by eye, no rebuild.
- Live update in the editor viewport (no Play needed).
- Control surface: the Floor Monitor gains a "Show in viewport" toggle plus per-layer toggles (labeled / unlabeled), marker size, and DataStream connect/disconnect.

**Out (YAGNI):**
- Skeleton bones / lines (explicitly cut).
- Marker name text labels in 3D.
- Marker trails / history / recording / export.
- Force-plate / device / EMG data from the SDK.
- A custom embedded viewport widget — we use the normal level viewport.
- Auto-deriving stage dimensions — handled later via the alignment knobs.

## Architecture

```
APCAPVolumeVisualizer (placeable AActor, ticks in editor)
  ├── IMarkerSource* ActiveSource           // pluggable feed
  │     ├── FLiveLinkMarkerSource   (Phase 1)
  │     └── FViconSDKMarkerSource   (Phase 2, WITH_VICON_SDK)
  ├── FVizFrame   CurrentFrame              // common data shape both sources fill
  └── rendering: TMap<FName,UInstancedStaticMeshComponent*> per-subject ISMs
                 + one UInstancedStaticMeshComponent for unlabeled
```

### `FVizFrame` — the common data shape
```cpp
struct FVizMarkerGroup {            // one labeled prop/subject
    FName   SubjectName;
    TArray<FVector> Points;         // already in UE world space (cm)
};
struct FVizFrame {
    TArray<FVizMarkerGroup> Labeled;
    TArray<FVector>         Unlabeled;   // empty in Phase 1
};
```
Both sources emit `FVizFrame`. Rendering and alignment are written **once** against this struct, so adding the SDK source is "fill the same struct," not "rewire the viz."

### `IMarkerSource` — the feed interface
```cpp
class IMarkerSource {
public:
    virtual ~IMarkerSource() {}
    virtual bool  IsAvailable() const = 0;
    virtual void  Poll(FVizFrame& OutFrame) = 0;   // fill OutFrame for this tick
};
```

### `APCAPVolumeVisualizer`
- `PrimaryActorTick.bCanEverTick = true`, `bAllowTickBeforeBeginPlay = true`.
- Override `ShouldTickIfViewportsOnly() { return true; }` — this is what makes it update live in the editor without entering PIE.
- Each tick: `ActiveSource->Poll(CurrentFrame)` → apply alignment transform → push instance transforms into the per-subject ISMs (reuse instances across ticks; grow/shrink as counts change).
- `UPROPERTY(EditAnywhere)` knobs:
  - `float UnitScale = 1.0f` (extra multiplier on top of the source's native conversion)
  - `FVector OriginOffset = (0,0,0)`
  - `float Yaw = 0.f`
  - `float MarkerSize = 1.0f` (instance scale)
  - `bool bShowLabeled = true`, `bool bShowUnlabeled = true`
  - SDK connection: `FString DataStreamHost = "localhost:801"`, `bool bConnectSDK`
- A small sphere static mesh (engine `BasicShapes/Sphere` or a plugin asset) is the ISM template.

### Rendering — instanced meshes
- One `UInstancedStaticMeshComponent` per labeled subject, its material a `UMaterialInstanceDynamic` with a `Color` param set to the subject's color. One ISM for unlabeled (neutral grey).
- Per tick: `BatchUpdateInstancesTransforms` (or Add/Update/Remove to match the new count). Avoid `ClearInstances` + re-add every frame where counts are stable — update in place to keep it cheap.
- Chosen over debug-draw (flickers, slow at hundreds of points) and Niagara (overkill, awkward to feed from C++).

## Data sources

### Phase 1 — `FLiveLinkMarkerSource` (no new dependency)
- `ILiveLinkClient::GetSubjects(false, false)` → for each, `GetSubjectRole_AnyThread`.
- For Animation-role subjects: `EvaluateFrame_AnyThread(name, ULiveLinkAnimationRole::StaticClass(), Frame)`:
  - `Frame.StaticData.Cast<FLiveLinkSkeletonStaticData>()` → bone parents/count.
  - `Frame.FrameData.Cast<FLiveLinkAnimationFrameData>()` → `Transforms` (one per bone).
  - Compose each bone down its parent chain (the root bone carries the subject's position in the volume) → one volume-space `FVector` per bone, in UE cm.
- A rigid prop streamed as Animation role with 1–2 bones yields 1–2 dots; an actor yields a joint cloud.
- Live Link already delivers UE-space cm, so the source's native conversion is identity; only the actor's alignment knobs apply.
- These are solved joint centers, **labeled as a stand-in in the UI**, not the physical markers.

### Phase 2 — `FViconSDKMarkerSource` (needs the SDK)
- Guarded by `WITH_VICON_SDK` (defined in Build.cs only when the SDK is present).
- `ViconDataStreamSDK::CPP::Client`: `Connect(Host)`, `EnableMarkerData()`, `EnableUnlabeledMarkerData()`, `GetFrame()` each Poll.
- Unlabeled: `GetUnlabeledMarkerCount()` / `GetUnlabeledMarkerGlobalTranslation(i)` → `FVizFrame::Unlabeled`.
- Labeled: per `GetSubjectName(s)`, `GetMarkerCount` / `GetMarkerGlobalTranslation(subject, marker)` → `FVizMarkerGroup`.
- Opens a **second** DataStream client alongside the Live Link plugin's — Shogun's server allows multiple clients.
- Native conversion: Vicon mm, Z-up, right-handed → UE cm, Z-up, left-handed (see below).

When the SDK source is connected, it supersedes the Live Link stand-in for labeled subjects; unlabeled markers only ever come from the SDK.

## Coordinate & scale

- **Vicon → UE conversion** (SDK source only) is a pure function so it is unit-testable and tunable:
  ```cpp
  FVector ViconMMToUE(const double T[3]);   // candidate: FVector(T[0], -T[1], T[2]) / 10.0
  ```
  The exact axis/handedness mapping (which axis to negate, whether to swap X/Y) **must be verified on the rig** — the Yaw knob and a `bFlipY`/axis option let us correct it empirically without a rebuild. Live Link's plugin already does this conversion internally for skeletons; we replicate it for raw markers.
- **FBX to scale:** import the FBX with "Convert Scene Unit" on; calibrate against one known real measurement *later*. Until then, the actor's `UnitScale` / `OriginOffset` / `Yaw` knobs align the dots to whatever scale the FBX comes in at. (FBX is not on this Mac; Madi merges it manually later.)

## Coloring
- Deterministic per-subject color: hash `SubjectName` → hue, fixed saturation/value → `FLinearColor`. Stable across runs, distinct per subject. Pure function → testable.
- Unlabeled markers: fixed neutral grey.

## Control UI
- Floor Monitor (`SPCAPFloorMonitor`) header gains:
  - **Show in viewport** toggle — spawns/destroys the `APCAPVolumeVisualizer` in the current editor world (follows the existing VCam-subsystem spawn pattern).
  - Per-layer toggles: Labeled, Unlabeled.
  - Marker size slider.
  - DataStream: host field + Connect/Disconnect (Phase 2; disabled/hidden when `WITH_VICON_SDK` is off, with a tooltip explaining the SDK is needed).
- The text list and the spatial view become one tool.

## SDK vendoring (Phase 2 prerequisite, supplied by Madi)
- Drop the Vicon DataStream SDK into `Plugins/PCAPTool/Source/ThirdParty/ViconDataStreamSDK/` (headers + Windows `.lib`/`.dll`). Source: the `ThirdParty` folder of the Windows `LiveLinkViconDataStream` plugin, or Vicon's free SDK download.
- `PCAPTool.Build.cs`: when those files exist, add the include path + link the lib + stage the DLL, and `PublicDefinitions.Add("WITH_VICON_SDK=1")`. When absent, `WITH_VICON_SDK=0` and the SDK source compiles out — Phase 1 still builds clean.

## Build phases
1. **Phase 1 (now, no deps):** `IMarkerSource`, `FVizFrame`, `APCAPVolumeVisualizer` (editor tick + ISM rendering + alignment knobs + color), `FLiveLinkMarkerSource`, Floor Monitor "Show in viewport" + per-layer toggles + size. Pure-function tests for the color hash. Proves coordinate alignment and to-scale rendering.
2. **Phase 2 (after SDK vendored):** `FViconSDKMarkerSource`, the mm→cm/handedness conversion (+ test), DataStream connect/disconnect UI, axis verification on-rig.

## Testing
- **Automation (pure functions, in `PCAPDataModelTests.cpp` style):**
  - `ViconMMToUE` — known input → expected UE vector (mm→cm divide; chosen handedness).
  - Per-subject color hash — determinism (same name → same color) + distinctness for a sample set.
- **Manual / on-rig:** dots appear, move live in the editor, register against the FBX after knob calibration; Phase 2 axis/handedness correct; second DataStream connection stable alongside Live Link.
- Cannot compile on this Mac (UE 5.7 builds on Windows) — author-verify against UE 5.4 API, Madi compiles.

## Files
**New:**
- `Public/PCAPVolumeVisualizer.h` / `Private/PCAPVolumeVisualizer.cpp` — the actor.
- `Public/PCAPMarkerSource.h` — `IMarkerSource` + `FVizFrame` + `FVizMarkerGroup` + color hash + `ViconMMToUE` decl.
- `Private/PCAPLiveLinkMarkerSource.cpp` — Phase 1 source.
- `Private/PCAPViconSDKMarkerSource.cpp` — Phase 2 source (compiled only when `WITH_VICON_SDK`).
- `Private/Tests/PCAPVolumeVizTests.cpp` — pure-function tests.

**Changed:**
- `SPCAPFloorMonitor.h/.cpp` — viewport toggle + layer toggles + size + connect/disconnect.
- `PCAPTool.Build.cs` — conditional Vicon SDK include/link/define (Phase 2).

## Risks / open items
- **SDK files** must be supplied by Madi (Windows-only); Phase 2 is blocked until then. Phase 1 is independent.
- **Axis/handedness** for raw markers needs on-rig verification — mitigated by the empirical Yaw/flip knobs.
- **Second DataStream connection** — assumed OK (Shogun allows multiple clients); verify it doesn't disturb the Live Link feed.
- **Stage dimensions / FBX** deferred — knobs absorb this; no first-try guarantee on scale until calibrated.
- **Parallel sessions** share this repo — touch only the files above; `SPCAPFloorMonitor` is shared with the VCam/Floor work, edit carefully.
