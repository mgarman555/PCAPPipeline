# Volume Visualizer — design spec

**Date:** 2026-06-15
**Status:** design — pending Madi's review (not yet built)
**Author:** Madi + Claude (brainstormed 2026-06-15)

## Goal

**Pull in a stage configuration → the whole stage comes up visually:** its FBX stage geometry, plus everything Vicon/Shogun is tracking shown as **marker dots with name labels**, sitting in the right place on the stage. Pulling in the stage auto-connects to *that stage's* Vicon DataStream and shows all markers + labels in scale, registered to the stage FBX.

The stage configuration is the single source of truth: which FBX, which DataStream address, and how the Vicon space aligns to the FBX. This is the spatial counterpart to the Floor Monitor (a read-only *text* list) — same data, projected into 3D on the stage.

No skeleton bones, no custom embedded viewport. Dots + labels in the normal level viewport.

## Scope

**In:**
- A placeable actor (`APCAPVolumeVisualizer`) that owns **both** the stage FBX mesh **and** the tracked-point rendering, so they stay locked together.
- Tracked points as instanced-mesh **dots**, one color per labeled prop/subject, neutral for unlabeled.
- A readable **name label** (text) per labeled prop/subject at its location.
- Two data sources behind one interface:
  - **Phase 1 — Live Link stand-in** (no new dependency): a dot at each labeled prop's tracked position and at each subject's joints; name labels per subject. Solved points, not physical markers — a stand-in to build and prove the pipeline.
  - **Phase 2 — Vicon DataStream SDK** (needs the SDK vendored in): the real marker cloud — unlabeled + per-subject labeled markers — and **auto-connect** to the stage's DataStream.
- **Stage-config integration:** the `UStageConfigAsset` carries the DataStream host + the per-stage alignment; "pull in the stage" spawns the actor configured from it, shows the FBX, and (Phase 2) connects.
- Per-stage alignment (Unit Scale / Origin Offset / Yaw) tuned by eye and **saved back to the stage config**, so it reproduces every time.
- Live update in the editor viewport (no Play needed).
- Per-layer toggles (labeled dots / unlabeled / labels) + marker size, surfaced on the actor and the Floor Monitor.

**Out (YAGNI):**
- Skeleton bones / lines (explicitly cut).
- Marker trails / history / recording / export.
- Force-plate / device / EMG data from the SDK.
- A custom embedded viewport widget — we use the normal level viewport.
- Auto-deriving stage dimensions — handled via the alignment knobs.
- Skeletal `StageReferenceMesh` (support static-mesh FBX first; skeletal deferred).

## Stage-config integration (the headline flow)

1. **Pull in the stage** — from the Stage database card ("Open stage view") and automatically when the day's stage is set in Call Sheet. *(Trigger location to confirm with Madi — could also be the Operator Console.)*
2. The flow spawns one `APCAPVolumeVisualizer` in the current editor world, handing it the `UStageConfigAsset`.
3. The actor:
   - shows `StageReferenceMesh` on its child `StageMesh` component (the FBX, at the actor origin),
   - applies the stored alignment (Unit Scale / Origin Offset / Yaw),
   - selects the data source: Phase 2 **auto-connects** the DataStream at `DataStreamHost` if `bAutoConnectDataStream`; otherwise falls back to the Live Link stand-in,
   - starts ticking → dots + labels appear on the stage.
4. Tune alignment by eye if needed → **"Save alignment to stage"** writes it back to the `UStageConfigAsset`.

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
APCAPVolumeVisualizer (placeable AActor, ticks in editor)
  ├── UStaticMeshComponent  StageMesh             // the stage FBX (from StageReferenceMesh)
  ├── TSoftObjectPtr<UStageConfigAsset> StageConfig  // source of host + alignment + mesh
  ├── IMarkerSource* ActiveSource                 // pluggable feed
  │     ├── FLiveLinkMarkerSource   (Phase 1)
  │     └── FViconSDKMarkerSource   (Phase 2, WITH_VICON_SDK)
  ├── FVizFrame   CurrentFrame                    // common data shape both sources fill
  ├── TMap<FName,UInstancedStaticMeshComponent*>  per-subject dot ISMs (+ one unlabeled)
  └── TMap<FName,UTextRenderComponent*>           per-subject name labels
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
- Each tick: `ActiveSource->Poll(CurrentFrame)` → apply alignment (`VizUnitScale`/`VizOriginOffset`/`VizYaw`) → update per-subject ISMs (reuse instances; grow/shrink to match counts) → move name labels to each group's centroid.
- `UPROPERTY(EditAnywhere)` live knobs mirror the config (`UnitScale`, `OriginOffset`, `Yaw`, `MarkerSize`, `bShowLabeled`, `bShowUnlabeled`, `bShowNames`); `CallInEditor` button **Save Alignment To Stage**.
- ISM template: a small sphere (`/Engine/BasicShapes/Sphere`).

### Rendering — instanced meshes + text
- One `UInstancedStaticMeshComponent` per labeled subject; material is a `UMaterialInstanceDynamic` with a `Color` param = the subject's color. One ISM for unlabeled (neutral grey).
- Per tick: `BatchUpdateInstancesTransforms` / Add / Remove to match counts; update in place when stable (avoid full clear+rebuild).
- One `UTextRenderComponent` per labeled subject, text = subject name, placed at the group centroid; hidden when `bShowNames` is false.
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

## Control UI
- **Stage database card:** "Open stage view" — runs the pull-in flow (spawn actor from this stage's config).
- **Call Sheet:** setting the day's active stage runs the same flow. *(Confirm trigger placement.)*
- **Floor Monitor:** "Show in viewport" toggle (spawns/destroys the actor for the active stage) + layer toggles (labeled / unlabeled / names) + marker size; the text list and spatial view become one tool.
- DataStream connect/disconnect is automatic from the stage config; a manual override is exposed on the actor (Phase 2). When `WITH_VICON_SDK` is off, SDK controls are hidden with a tooltip explaining the SDK is needed.

## SDK vendoring (Phase 2 prerequisite, supplied by Madi)
- Drop the Vicon DataStream SDK into `Plugins/PCAPTool/Source/ThirdParty/ViconDataStreamSDK/` (headers + Windows `.lib`/`.dll`). Source: the `ThirdParty` folder of the Windows `LiveLinkViconDataStream` plugin, or Vicon's free download.
- `PCAPTool.Build.cs`: when present, add include path + link lib + stage the DLL + `PublicDefinitions.Add("WITH_VICON_SDK=1")`. When absent → `WITH_VICON_SDK=0`, the SDK source compiles out, Phase 1 builds clean.

## Build phases
1. **Phase 1 (now, no deps):** `IMarkerSource`, `FVizFrame`, `APCAPVolumeVisualizer` (editor tick, ISM dots, text labels, alignment, color, StageMesh from config, Save-to-stage), `FLiveLinkMarkerSource`, `UStageConfigAsset` field additions, the pull-in flow from the Stage DB (+ Call Sheet), Floor Monitor toggles. Pure-function tests (color hash). Proves alignment + to-scale rendering + the stage-config flow.
2. **Phase 2 (after SDK vendored):** `FViconSDKMarkerSource`, mm→cm/handedness conversion (+ test), auto-connect from the stage's `DataStreamHost`, on-rig axis verification.

## Testing
- **Automation (pure functions, `PCAPDataModelTests.cpp` style):**
  - `ViconMMToUE` — known input → expected UE vector.
  - Per-subject color hash — determinism + distinctness.
- **Manual / on-rig:** pull in a stage → FBX + dots + labels appear, update live, register after knob calibration, alignment persists via save-to-stage; Phase 2 auto-connects, axis/handedness correct, second DataStream connection stable alongside Live Link.
- Cannot compile on this Mac (UE 5.7 builds on Windows) — author-verify against UE 5.4 API, Madi compiles.

## Files
**New:**
- `Public/PCAPVolumeVisualizer.h` / `Private/PCAPVolumeVisualizer.cpp` — the actor + pull-in spawn helper.
- `Public/PCAPMarkerSource.h` — `IMarkerSource` + `FVizFrame` + `FVizMarkerGroup` + color hash + `ViconMMToUE` decl.
- `Private/PCAPLiveLinkMarkerSource.cpp` — Phase 1 source.
- `Private/PCAPViconSDKMarkerSource.cpp` — Phase 2 source (compiled only when `WITH_VICON_SDK`).
- `Private/Tests/PCAPVolumeVizTests.cpp` — pure-function tests.

**Changed:**
- `StageConfigAsset.h` — `DataStreamHost`, `bAutoConnectDataStream`, `VizUnitScale`, `VizOriginOffset`, `VizYaw`.
- `SPCAPStageDatabasePanel.h/.cpp` — "Open stage view" action.
- `SPCAPCallSheetPanel.cpp` — run the flow when the day's stage is set *(pending trigger confirmation)*.
- `SPCAPFloorMonitor.h/.cpp` — viewport toggle + layer toggles + size.
- `PCAPTool.Build.cs` — conditional Vicon SDK include/link/define (Phase 2).

## Risks / open items
- **Trigger placement** for "pull in the stage configuration" — proposed Stage DB + Call Sheet; confirm with Madi.
- **SDK files** supplied by Madi (Windows-only); Phase 2 blocked until then. Phase 1 independent.
- **Axis/handedness** for raw markers verified on-rig — mitigated by empirical Yaw/flip knobs.
- **Second DataStream connection** assumed OK (Shogun allows multiple clients); verify it doesn't disturb Live Link.
- **Stage dimensions / FBX** deferred — alignment knobs absorb this; no first-try scale guarantee until calibrated.
- **Parallel sessions** share this repo — `SPCAPStageDatabasePanel`, `SPCAPCallSheetPanel`, `SPCAPFloorMonitor`, `StageConfigAsset.h`, `PCAPToolTypes.h` may be touched by other work; edit only the regions above, carefully.
