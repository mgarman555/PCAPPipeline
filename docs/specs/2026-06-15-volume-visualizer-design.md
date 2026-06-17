# Volume Visualizer — design spec

**Date:** 2026-06-15
**Status:** design — approved direction (Madi, 2026-06-15); not yet built
**Author:** Madi + Claude (brainstormed 2026-06-15)

## Goal

A **placeable Blueprint actor** that, given a stage configuration, brings the stage up visually: its FBX geometry, plus everything Vicon/Shogun is tracking shown as **marker dots with name labels**, sitting in the right place on the stage and in scale.

"Pull in the stage configuration" = drop the actor in a level and **assign a `UStageConfigAsset`** in its Details. The actor reads that stage's FBX, its DataStream address, and its alignment, then shows the markers + labels registered onto the FBX. Self-contained — **no tool-panel wiring in this phase** (you grab/place the actor from the asset browser).

This is the spatial counterpart to the Floor Monitor (a read-only *text* list) — same data, projected into 3D on the stage. No skeleton bones, no custom embedded viewport.

## Scope

**In:**
- `APCAPVolumeVisualizer` — a `Blueprintable`, placeable actor that owns **both** the stage FBX mesh **and** the tracked-point rendering, so they stay locked together. Drag it (or a BP subclass) in from the Place Actors / content browser.
- Configured by assigning a `UStageConfigAsset` (preferred) or via inline Details fields (host + alignment) when no config is set.
- Tracked points as instanced-mesh **dots**, one color per labeled prop/subject, neutral for unlabeled.
- A readable **name label** (text) per labeled prop/subject at its location.
- Two data sources behind one interface:
  - **Phase 1 — Live Link stand-in** (no new dependency): a dot at each labeled prop's tracked position and at each subject's joints; name labels per subject. Solved points, not physical markers — a stand-in to build and prove the pipeline.
  - **Phase 2 — Vicon DataStream SDK** (needs the SDK vendored in): the real marker cloud — unlabeled + per-subject labeled markers — auto-connecting to the stage's DataStream.
- Per-stage alignment (Unit Scale / Origin Offset / Yaw) tuned by eye and **saved back to the stage config**, so it reproduces every time.
- Live update in the editor viewport (no Play needed).
- Per-layer toggles (labeled dots / unlabeled / names) + marker size, on the actor's Details.

**Out (YAGNI):**
- Tool-panel wiring (Stage DB / Call Sheet / Operator / Floor Monitor) — deferred; configured on the actor for now.
- Skeleton bones / lines (explicitly cut).
- Marker trails / history / recording / export.
- Force-plate / device / EMG data from the SDK.
- A custom embedded viewport widget — we use the normal level viewport.
- Auto-deriving stage dimensions — handled via the alignment knobs.
- Skeletal `StageReferenceMesh` (support static-mesh FBX first; skeletal deferred).

## Configuring the actor (the flow)

1. **Place** `APCAPVolumeVisualizer` (or a BP subclass) in the level from the asset browser.
2. **Assign** a `UStageConfigAsset` to its `StageConfig` property.
3. On assignment / construction the actor:
   - shows `StageReferenceMesh` on its `StageMesh` component (the FBX, at the actor origin),
   - copies the config's alignment (Unit Scale / Origin Offset / Yaw) into its live knobs,
   - picks the source: Phase 2 auto-connects the DataStream at `DataStreamHost` if `bAutoConnectDataStream`; otherwise the Live Link stand-in,
   - ticks → dots + labels appear on the stage.
4. Tune alignment by eye if needed → the **Save Alignment To Stage** button writes it back to the `UStageConfigAsset`.

With no `StageConfig` assigned, the actor uses its own inline Details fields (host + alignment + an optional mesh override), so it still works standalone.

### `UStageConfigAsset` additions
```cpp
// Vicon DataStream address for this stage's raw-marker feed (Phase 2 / SDK).
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
FString DataStreamHost = TEXT("localhost:801");

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
bool bAutoConnectDataStream = true;

// Calibration that registers Vicon space onto this stage's FBX. Tuned once, saved here.
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
float VizUnitScale = 1.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
FVector VizOriginOffset = FVector::ZeroVector;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
float VizYaw = 0.0f;
```
(`StageReferenceMesh` and `LiveLinkPresetPath` already exist; the host is the SDK feed, the preset is the Live Link feed — they coexist.)

## Architecture

```
APCAPVolumeVisualizer : AActor   // UCLASS(Blueprintable, placeable); ticks in editor
  ├── UStaticMeshComponent  StageMesh                 // the stage FBX (from StageReferenceMesh)
  ├── TSoftObjectPtr<UStageConfigAsset> StageConfig   // source of host + alignment + mesh
  ├── IMarkerSource* ActiveSource                     // pluggable feed
  │     ├── FLiveLinkMarkerSource   (Phase 1)
  │     └── FViconSDKMarkerSource   (Phase 2, WITH_VICON_SDK)
  ├── FVizFrame   CurrentFrame                        // common data shape both sources fill
  ├── TMap<FName,UInstancedStaticMeshComponent*>      per-subject dot ISMs (+ one unlabeled)
  └── TMap<FName,UTextRenderComponent*>               per-subject name labels
```

### `FVizFrame` — common data shape
```cpp
struct FVizMarkerGroup {            // one labeled prop/subject
    FName   SubjectName;
    TArray<FVector> Points;         // UE world space (cm), pre-alignment
};
struct FVizFrame {
    TArray<FVizMarkerGroup> Labeled;
    TArray<FVector>         Unlabeled;   // empty in Phase 1
};
```
Both sources emit `FVizFrame`; rendering + alignment are written once against it.

### `IMarkerSource`
```cpp
class IMarkerSource {
public:
    virtual ~IMarkerSource() {}
    virtual bool IsAvailable() const = 0;
    virtual bool Connect(const FString& Host) { return true; }   // SDK; no-op for Live Link
    virtual void Disconnect() {}
    virtual void Poll(FVizFrame& OutFrame) = 0;
};
```

### `APCAPVolumeVisualizer`
- `PrimaryActorTick.bCanEverTick = true`, `bAllowTickBeforeBeginPlay = true`, `ShouldTickIfViewportsOnly() → true` (live in-editor without PIE).
- `OnConstruction` / a `RefreshFromStageConfig` `CallInEditor` button: load `StageReferenceMesh` into `StageMesh`, copy alignment from config.
- Each tick: `ActiveSource->Poll(CurrentFrame)` → apply alignment (`UnitScale`/`OriginOffset`/`Yaw`) → update per-subject ISMs (reuse instances; grow/shrink to match counts) → move name labels to each group's centroid.
- `EditAnywhere` live knobs: `UnitScale`, `OriginOffset`, `Yaw`, `MarkerSize`, `bShowLabeled`, `bShowUnlabeled`, `bShowNames`; `CallInEditor` buttons: `RefreshFromStageConfig`, `SaveAlignmentToStage`, `Connect`/`Disconnect` (Phase 2).
- ISM template: a small sphere (`/Engine/BasicShapes/Sphere`).

### Rendering — instanced meshes + text
- One `UInstancedStaticMeshComponent` per labeled subject; material is a `UMaterialInstanceDynamic` with a `Color` param = the subject's color. One ISM for unlabeled (neutral grey).
- Per tick: `BatchUpdateInstancesTransforms` / Add / Remove to match counts; update in place when stable (avoid full clear+rebuild).
- One `UTextRenderComponent` per labeled subject, text = subject name, at the group centroid; hidden when `bShowNames` is false.
- Chosen over debug-draw (flickers, slow at hundreds of points) and Niagara (overkill from C++).

## Data sources

### Phase 1 — `FLiveLinkMarkerSource` (no new dependency)
- `ILiveLinkClient::GetSubjects(false,false)` → for each Animation-role subject, `EvaluateFrame_AnyThread(name, ULiveLinkAnimationRole::StaticClass(), Frame)`:
  - `Frame.StaticData.Cast<FLiveLinkSkeletonStaticData>()` → bone parents.
  - `Frame.FrameData.Cast<FLiveLinkAnimationFrameData>()` → `Transforms` (one per bone).
  - Compose each bone down its parent chain (root carries the subject's volume position) → one volume-space `FVector` per bone, UE cm.
- A rigid prop (1–2 bones) → 1–2 dots; an actor → a joint cloud. Name label = subject name.
- Live Link already delivers UE-space cm → source's native conversion is identity; only the actor/stage alignment applies.
- Labeled in the UI as a **stand-in** (solved joint centers, not physical markers).

### Phase 2 — `FViconSDKMarkerSource` (needs the SDK)
- Guarded by `WITH_VICON_SDK` (set in Build.cs only when the SDK is present).
- `ViconDataStreamSDK::CPP::Client`: `Connect(Host)` on open, `EnableMarkerData()` + `EnableUnlabeledMarkerData()`, `GetFrame()` each Poll.
- Unlabeled: `GetUnlabeledMarkerCount()` / `GetUnlabeledMarkerGlobalTranslation(i)` → `FVizFrame::Unlabeled`.
- Labeled: per `GetSubjectName(s)`, `GetMarkerCount`/`GetMarkerGlobalTranslation(subject, marker)` → `FVizMarkerGroup`.
- Opens a **second** DataStream client alongside the Live Link plugin's — Shogun allows multiple clients.
- Native conversion: Vicon mm/Z-up/right-handed → UE cm/Z-up/left-handed (below).
- When connected, supersedes the Live Link stand-in for labeled subjects; unlabeled only ever come from the SDK.

## Coordinate & scale
- **Vicon → UE conversion** (SDK source) as a pure, unit-testable function:
  ```cpp
  FVector ViconMMToUE(const double T[3]);   // candidate: FVector(T[0], -T[1], T[2]) / 10.0
  ```
  Exact axis/handedness mapping **verified on-rig**; the Yaw knob + an axis/flip option correct it empirically without a rebuild. (Live Link's plugin does this internally for skeletons; we replicate it for raw markers.)
- **FBX to scale:** import with "Convert Scene Unit" on; calibrate against one real measurement *later*. Until then the per-stage `VizUnitScale`/`VizOriginOffset`/`VizYaw` align the dots to whatever scale the FBX comes in at. (FBX merged manually later; not on this Mac.)

## Coloring
- Deterministic per-subject color: hash `SubjectName` → hue, fixed S/V → `FLinearColor`. Stable across runs, distinct per subject. Pure function → testable.
- Unlabeled markers: fixed neutral grey.

## SDK vendoring (Phase 2 prerequisite, supplied by Madi)
- Drop the Vicon DataStream SDK into `Plugins/PCAPTool/Source/ThirdParty/ViconDataStreamSDK/` (headers + Windows `.lib`/`.dll`). Source: the `ThirdParty` folder of the Windows `LiveLinkViconDataStream` plugin, or Vicon's free download.
- `PCAPTool.Build.cs`: when present, add include path + link lib + stage the DLL + `PublicDefinitions.Add("WITH_VICON_SDK=1")`. When absent → `WITH_VICON_SDK=0`, the SDK source compiles out, Phase 1 builds clean.

## Build phases
1. **Phase 1 (now, no deps):** `IMarkerSource`, `FVizFrame`, `APCAPVolumeVisualizer` (Blueprintable/placeable, editor tick, ISM dots, text labels, alignment, color, StageMesh + RefreshFromStageConfig + SaveAlignmentToStage), `FLiveLinkMarkerSource`, `UStageConfigAsset` field additions. Pure-function tests (color hash). Proves alignment + to-scale rendering + the config-driven flow.
2. **Phase 2 (after SDK vendored):** `FViconSDKMarkerSource`, mm→cm/handedness conversion (+ test), auto-connect from the stage's `DataStreamHost`, on-rig axis verification.

## Testing
- **Automation (pure functions, `PCAPDataModelTests.cpp` style):**
  - `ViconMMToUE` — known input → expected UE vector.
  - Per-subject color hash — determinism + distinctness.
- **Manual / on-rig:** place the actor, assign a stage config → FBX + dots + labels appear, update live, register after knob calibration, alignment persists via Save-to-stage; Phase 2 auto-connects, axis/handedness correct, second DataStream connection stable alongside Live Link.
- Cannot compile on this Mac (UE 5.7 builds on Windows) — author-verify against UE 5.4 API, Madi compiles.

## Files
**New:**
- `Public/PCAPVolumeVisualizer.h` / `Private/PCAPVolumeVisualizer.cpp` — the actor.
- `Public/PCAPMarkerSource.h` — `IMarkerSource` + `FVizFrame` + `FVizMarkerGroup` + color hash + `ViconMMToUE` decl.
- `Private/PCAPLiveLinkMarkerSource.cpp` — Phase 1 source.
- `Private/PCAPViconSDKMarkerSource.cpp` — Phase 2 source (compiled only when `WITH_VICON_SDK`).
- `Private/Tests/PCAPVolumeVizTests.cpp` — pure-function tests.

**Changed:**
- `StageConfigAsset.h` — `DataStreamHost`, `bAutoConnectDataStream`, `VizUnitScale`, `VizOriginOffset`, `VizYaw`.
- `PCAPTool.Build.cs` — conditional Vicon SDK include/link/define (Phase 2).

## Risks / open items
- **SDK files** supplied by Madi (Windows-only); Phase 2 blocked until then. Phase 1 independent.
- **Axis/handedness** for raw markers verified on-rig — mitigated by empirical Yaw/flip knobs.
- **Second DataStream connection** assumed OK (Shogun allows multiple clients); verify it doesn't disturb Live Link.
- **Stage dimensions / FBX** deferred — alignment knobs absorb this; no first-try scale guarantee until calibrated.
- **`StageReferenceMesh` is `TSoftObjectPtr<UObject>`** — Phase 1 handles `UStaticMesh`; if a stage uses a skeletal mesh, that's deferred.
- **Parallel sessions** share `StageConfigAsset.h` and `PCAPTool.Build.cs` — edit only the regions above, carefully (guarded commits).
