# Volume Visualizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A placeable Blueprint actor that, given a `UStageConfigAsset`, shows the stage FBX plus every Vicon/Shogun-tracked subject as colored marker dots with name labels, registered to the stage and live in the editor viewport.

**Architecture:** One `APCAPVolumeVisualizer` actor owns the stage mesh + the marker rendering. A pluggable `IMarkerSource` fills a common `FVizFrame` each tick; Phase 1 ships the Live Link stand-in source (no new dependency). Pure math (per-subject color, Vicon→UE conversion) is isolated in `PCAPViz::` free functions and unit-tested via UE automation.

**Tech Stack:** UE 5.7 C++, Slate-free actor, Live Link (`ILiveLinkClient`, Animation role), Instanced Static Mesh + Text Render components, UE Automation tests.

**Build/test reality:** This Mac has only UE 5.4 (API reference); the project compiles on Windows. Author code here, Madi compiles. Automation tests run in **Session Frontend → Automation → filter `PCAP.Viz`**. Use the guarded push pattern for every commit (`git fetch; git merge-base --is-ancestor origin/main HEAD && push || rebase`).

**Phase 1 only** (this plan). Phase 2 (Vicon DataStream SDK raw markers) is blocked on Madi vendoring the SDK and gets its own plan — outlined at the end.

---

## File structure (Phase 1)

| File | Responsibility |
|------|----------------|
| `Public/PCAPMarkerSource.h` (new) | `FVizFrame`, `FVizMarkerGroup`, `IMarkerSource` interface, `PCAPViz::SubjectColor`, `PCAPViz::ViconMMToUE` decls |
| `Private/PCAPMarkerSource.cpp` (new) | pure-function impls (color hash, Vicon→UE) |
| `Private/PCAPLiveLinkMarkerSource.h` (new) | `FLiveLinkMarkerSource : IMarkerSource` decl |
| `Private/PCAPLiveLinkMarkerSource.cpp` (new) | Live Link enumeration + joint composition |
| `Public/PCAPVolumeVisualizer.h` (new) | the actor |
| `Private/PCAPVolumeVisualizer.cpp` (new) | actor impl (components, tick, render, config) |
| `Private/Tests/PCAPVolumeVizTests.cpp` (new) | automation tests for the pure functions |
| `Public/StageConfigAsset.h` (modify) | + `DataStreamHost`, `bAutoConnectDataStream`, `VizUnitScale`, `VizOriginOffset`, `VizYaw` |

No `Build.cs` change for Phase 1 — `LiveLink` + `LiveLinkInterface` are already dependencies.

---

## Task 1: Pure types + functions + automation tests (TDD)

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPMarkerSource.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPMarkerSource.cpp`
- Test: `Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPVolumeVizTests.cpp`

- [ ] **Step 1: Write the header** (`PCAPMarkerSource.h`)

```cpp
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
```

- [ ] **Step 2: Write the failing tests** (`Private/Tests/PCAPVolumeVizTests.cpp`)

```cpp
#include "Misc/AutomationTest.h"
#include "PCAPMarkerSource.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVizColorDeterministic, "PCAP.Viz.ColorDeterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVizColorDeterministic::RunTest(const FString&)
{
    const FLinearColor A = PCAPViz::SubjectColor(FName("kevinDorman"));
    const FLinearColor B = PCAPViz::SubjectColor(FName("kevinDorman"));
    TestTrue("same name -> same color", A.Equals(B, 0.0001f));
    const FLinearColor C = PCAPViz::SubjectColor(FName("Lightsaber01"));
    TestTrue("different names -> different color", !A.Equals(C, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVizViconConvert, "PCAP.Viz.ViconConvert",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVizViconConvert::RunTest(const FString&)
{
    const FVector V = PCAPViz::ViconMMToUE(1000.0, 2000.0, 3000.0);
    TestEqual("mm->cm X", V.X, 100.f);
    TestEqual("flip + mm->cm Y", V.Y, -200.f);
    TestEqual("mm->cm Z", V.Z, 300.f);
    return true;
}
```

- [ ] **Step 3: Implement the pure functions** (`PCAPMarkerSource.cpp`)

```cpp
#include "PCAPMarkerSource.h"

FLinearColor PCAPViz::SubjectColor(FName SubjectName)
{
    // djb2 over the string -> stable across runs (FName's type hash is not).
    const FString S = SubjectName.ToString();
    uint32 Hash = 5381;
    for (const TCHAR C : S) { Hash = ((Hash << 5) + Hash) + (uint32)C; }
    const uint8 Hue = (uint8)(Hash % 256);
    return FLinearColor::MakeFromHSV8(Hue, 200, 235);
}

FVector PCAPViz::ViconMMToUE(double X, double Y, double Z)
{
    // mm -> cm (x0.1); negate Y for right- to left-handed.
    return FVector((float)(X * 0.1), (float)(-Y * 0.1), (float)(Z * 0.1));
}
```

- [ ] **Step 4: Verify** — Madi compiles on Windows, then Session Frontend → Automation → run `PCAP.Viz.ColorDeterministic` and `PCAP.Viz.ViconConvert`. Expected: both PASS.

- [ ] **Step 5: Commit** (guarded push)

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPMarkerSource.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/PCAPMarkerSource.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/Tests/PCAPVolumeVizTests.cpp
git commit -m "feat(pcap): viz marker types + per-subject color + Vicon->UE conversion (tested)"
```

---

## Task 2: StageConfigAsset fields

**Files:** Modify `Plugins/PCAPTool/Source/PCAPTool/Public/StageConfigAsset.h`

- [ ] **Step 1: Add the fields** after `FString Notes;` (before the closing `};`)

```cpp
    // ── Volume Visualizer ──────────────────────────────────────────────
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

- [ ] **Step 2: Verify** — Madi compiles; open any `UStageConfigAsset` → the "Stage|Volume" fields show in Details.

- [ ] **Step 3: Commit** (guarded push)

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/StageConfigAsset.h
git commit -m "feat(pcap): stage config carries DataStream host + per-stage viz alignment"
```

---

## Task 3: Live Link marker source

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPLiveLinkMarkerSource.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPLiveLinkMarkerSource.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "PCAPMarkerSource.h"

// Phase 1 stand-in: solved skeleton joints (and prop transforms) from Live Link,
// as marker dots. Not the physical optical markers — those need the Vicon SDK (Phase 2).
class FLiveLinkMarkerSource : public IMarkerSource
{
public:
    virtual bool IsAvailable() const override;
    virtual void Poll(FVizFrame& OutFrame) override;
};
```

- [ ] **Step 2: Implementation** — enumerate Animation-role subjects, compose each bone to volume space

```cpp
#include "PCAPLiveLinkMarkerSource.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "LiveLinkTypes.h"

static ILiveLinkClient* GetLLClient()
{
    IModularFeatures& MF = IModularFeatures::Get();
    if (MF.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        return &MF.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    }
    return nullptr;
}

bool FLiveLinkMarkerSource::IsAvailable() const
{
    return GetLLClient() != nullptr;
}

void FLiveLinkMarkerSource::Poll(FVizFrame& OutFrame)
{
    OutFrame.Reset();
    ILiveLinkClient* Client = GetLLClient();
    if (!Client) { return; }

    for (const FLiveLinkSubjectKey& Key : Client->GetSubjects(/*disabled*/false, /*virtual*/false))
    {
        TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(Key);
        if (!Role || !Role->IsChildOf(ULiveLinkAnimationRole::StaticClass())) { continue; }

        FLiveLinkSubjectFrameData Data;
        if (!Client->EvaluateFrame_AnyThread(Key.SubjectName, ULiveLinkAnimationRole::StaticClass(), Data)) { continue; }

        const FLiveLinkSkeletonStaticData* Skel = Data.StaticData.Cast<FLiveLinkSkeletonStaticData>();
        const FLiveLinkAnimationFrameData* Anim = Data.FrameData.Cast<FLiveLinkAnimationFrameData>();
        if (!Skel || !Anim) { continue; }

        const TArray<int32>& Parents = Skel->GetBoneParents();
        const TArray<FTransform>& Locals = Anim->Transforms;
        if (Parents.Num() != Locals.Num() || Locals.Num() == 0) { continue; }

        // Compose parent-relative bone transforms down the hierarchy.
        // Live Link bones are ordered parent-before-child, so a single forward pass works.
        TArray<FTransform> World;
        World.SetNum(Locals.Num());
        for (int32 i = 0; i < Locals.Num(); ++i)
        {
            const int32 P = Parents[i];
            World[i] = (P >= 0 && P < i) ? (Locals[i] * World[P]) : Locals[i];
        }

        FVizMarkerGroup Group;
        Group.SubjectName = Key.SubjectName.Name;
        Group.Points.Reserve(World.Num());
        for (const FTransform& T : World) { Group.Points.Add(T.GetLocation()); }
        OutFrame.Labeled.Add(MoveTemp(Group));
    }
}
```

- [ ] **Step 3: Verify** — compiles as part of Task 5 (no standalone test; integration-verified in the viewport). No commit yet — bundle with Task 4 so the source has a consumer.

---

## Task 4: The actor — header + skeleton (compiles, places, shows stage mesh)

**Files:**
- Create: `Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVolumeVisualizer.h`
- Create: `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVolumeVisualizer.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCAPMarkerSource.h"
#include "PCAPVolumeVisualizer.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UStaticMesh;
class UMaterialInterface;
class UStageConfigAsset;

// Placeable Blueprint actor. Assign a UStageConfigAsset to pull in the stage FBX,
// alignment, and (Phase 2) DataStream; tracked subjects render as marker dots + labels.
UCLASS(Blueprintable, placeable)
class PCAPTOOL_API APCAPVolumeVisualizer : public AActor
{
    GENERATED_BODY()

public:
    APCAPVolumeVisualizer();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    TSoftObjectPtr<UStageConfigAsset> StageConfig;

    // Optional unlit material with a "Color" VectorParameter. Without it, dots are
    // uncolored but name labels still distinguish subjects.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Display")
    UMaterialInterface* DotMaterialBase = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment")
    float UnitScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment")
    FVector OriginOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment")
    float Yaw = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Display", meta=(ClampMin="0.1"))
    float MarkerSize = 3.f;   // approx cm diameter
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Display")
    bool bShowLabeled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Display")
    bool bShowUnlabeled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Display")
    bool bShowNames = true;

    UFUNCTION(CallInEditor, Category="Stage")
    void RefreshFromStageConfig();
    UFUNCTION(CallInEditor, Category="Alignment")
    void SaveAlignmentToStage();

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
#endif

private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> StageMesh;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> UnlabeledISM;
    UPROPERTY() TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> SubjectISMs;
    UPROPERTY() TMap<FName, TObjectPtr<UTextRenderComponent>> SubjectLabels;
    UPROPERTY() TObjectPtr<UStaticMesh> DotMesh;

    TSharedPtr<IMarkerSource> Source;
    FVizFrame Frame;

    void EnsureSource();
    void ApplyAlignment(FVector& P) const;
    UInstancedStaticMeshComponent* GetOrCreateSubjectISM(FName Subject);
    UTextRenderComponent* GetOrCreateLabel(FName Subject);
};
```

- [ ] **Step 2: Constructor + config + helpers** (`PCAPVolumeVisualizer.cpp`, part 1)

```cpp
#include "PCAPVolumeVisualizer.h"
#include "PCAPLiveLinkMarkerSource.h"
#include "StageConfigAsset.h"

#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

APCAPVolumeVisualizer::APCAPVolumeVisualizer()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    bAllowTickBeforeBeginPlay = true;

    StageMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StageMesh"));
    RootComponent = StageMesh;
    StageMesh->SetMobility(EComponentMobility::Movable);

    UnlabeledISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("UnlabeledISM"));
    UnlabeledISM->SetupAttachment(RootComponent);
    UnlabeledISM->SetMobility(EComponentMobility::Movable);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Sphere.Succeeded())
    {
        DotMesh = Sphere.Object;
        UnlabeledISM->SetStaticMesh(DotMesh);
    }
}

void APCAPVolumeVisualizer::RefreshFromStageConfig()
{
    UStageConfigAsset* Cfg = StageConfig.LoadSynchronous();
    if (!Cfg) { return; }

    if (UStaticMesh* SM = Cast<UStaticMesh>(Cfg->StageReferenceMesh.LoadSynchronous()))
    {
        StageMesh->SetStaticMesh(SM);
    }
    UnitScale    = Cfg->VizUnitScale;
    OriginOffset = Cfg->VizOriginOffset;
    Yaw          = Cfg->VizYaw;
}

void APCAPVolumeVisualizer::SaveAlignmentToStage()
{
    UStageConfigAsset* Cfg = StageConfig.LoadSynchronous();
    if (!Cfg) { return; }
    Cfg->VizUnitScale    = UnitScale;
    Cfg->VizOriginOffset = OriginOffset;
    Cfg->VizYaw          = Yaw;
#if WITH_EDITOR
    Cfg->MarkPackageDirty();
#endif
}

#if WITH_EDITOR
void APCAPVolumeVisualizer::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshFromStageConfig();
}
#endif

void APCAPVolumeVisualizer::EnsureSource()
{
    if (!Source.IsValid()) { Source = MakeShared<FLiveLinkMarkerSource>(); }
}

void APCAPVolumeVisualizer::ApplyAlignment(FVector& P) const
{
    P *= UnitScale;
    P = FRotator(0.f, Yaw, 0.f).RotateVector(P);
    P += OriginOffset;
}

UInstancedStaticMeshComponent* APCAPVolumeVisualizer::GetOrCreateSubjectISM(FName Subject)
{
    if (TObjectPtr<UInstancedStaticMeshComponent>* Found = SubjectISMs.Find(Subject))
    {
        return *Found;
    }
    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
    ISM->SetupAttachment(RootComponent);
    ISM->SetMobility(EComponentMobility::Movable);
    ISM->SetStaticMesh(DotMesh);
    ISM->RegisterComponent();
    if (DotMaterialBase)
    {
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(DotMaterialBase, this);
        MID->SetVectorParameterValue(TEXT("Color"), PCAPViz::SubjectColor(Subject));
        ISM->SetMaterial(0, MID);
    }
    SubjectISMs.Add(Subject, ISM);
    return ISM;
}

UTextRenderComponent* APCAPVolumeVisualizer::GetOrCreateLabel(FName Subject)
{
    if (TObjectPtr<UTextRenderComponent>* Found = SubjectLabels.Find(Subject))
    {
        return *Found;
    }
    UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
    Label->SetupAttachment(RootComponent);
    Label->SetMobility(EComponentMobility::Movable);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(12.f);
    Label->SetTextRenderColor(FColor::White);
    Label->RegisterComponent();
    SubjectLabels.Add(Subject, Label);
    return Label;
}
```

- [ ] **Step 3: Verify** — Madi compiles; the actor appears in Place Actors; dropping it shows the (empty) stage mesh slot; assigning a `StageConfig` with a static `StageReferenceMesh` shows that mesh. No dots yet. No commit yet — bundle with Task 5.

---

## Task 5: The tick — render dots + labels

**Files:** Modify `Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVolumeVisualizer.cpp` (append)

- [ ] **Step 1: Implement `Tick`**

```cpp
void APCAPVolumeVisualizer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    EnsureSource();
    if (!Source.IsValid()) { return; }
    Source->Poll(Frame);

    const FVector DotScale(MarkerSize / 100.f);   // engine sphere is 100cm dia → cm-ish

    // Labeled subjects → per-subject ISM + centroid label.
    TSet<FName> Seen;
    for (const FVizMarkerGroup& G : Frame.Labeled)
    {
        Seen.Add(G.SubjectName);

        UInstancedStaticMeshComponent* ISM = GetOrCreateSubjectISM(G.SubjectName);
        ISM->SetVisibility(bShowLabeled);
        ISM->ClearInstances();

        FVector Centroid = FVector::ZeroVector;
        for (FVector P : G.Points)
        {
            ApplyAlignment(P);
            Centroid += P;
            ISM->AddInstance(FTransform(FRotator::ZeroRotator, P, DotScale), /*bWorldSpace*/true);
        }

        UTextRenderComponent* Label = GetOrCreateLabel(G.SubjectName);
        const bool bHasPts = G.Points.Num() > 0;
        Label->SetVisibility(bShowNames && bHasPts);
        if (bHasPts)
        {
            Centroid /= G.Points.Num();
            Label->SetWorldLocation(Centroid + FVector(0.f, 0.f, 20.f));
            Label->SetText(FText::FromName(G.SubjectName));
        }
    }

    // Stale subjects (gone this frame) → clear + hide.
    for (const TPair<FName, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : SubjectISMs)
    {
        if (!Seen.Contains(Pair.Key) && Pair.Value) { Pair.Value->ClearInstances(); }
    }
    for (const TPair<FName, TObjectPtr<UTextRenderComponent>>& Pair : SubjectLabels)
    {
        if (!Seen.Contains(Pair.Key) && Pair.Value) { Pair.Value->SetVisibility(false); }
    }

    // Unlabeled markers (empty in Phase 1).
    if (UnlabeledISM)
    {
        UnlabeledISM->SetVisibility(bShowUnlabeled);
        UnlabeledISM->ClearInstances();
        for (FVector P : Frame.Unlabeled)
        {
            ApplyAlignment(P);
            UnlabeledISM->AddInstance(FTransform(FRotator::ZeroRotator, P, DotScale), /*bWorldSpace*/true);
        }
    }
}
```

> Note: `ClearInstances` + `AddInstance` every tick is the simple, correct first version (joint counts are small — dozens). If perf bites with Phase-2 marker clouds, switch to `BatchUpdateInstancesTransforms` with in-place grow/shrink. Deferred — not needed for Phase 1.

- [ ] **Step 2: Verify** — Madi compiles; start Shogun + Live Link streaming; drop the actor (assign a stage config). Expected: a cluster of dots per streaming subject, a white name label above each, both updating live in the editor viewport without pressing Play. Toggle `bShowLabeled` / `bShowNames` and tweak `OriginOffset` / `Yaw` / `UnitScale` → dots move accordingly.

- [ ] **Step 3: Commit** (guarded push — Tasks 3-5 together: the actor + its source)

```bash
git add Plugins/PCAPTool/Source/PCAPTool/Public/PCAPVolumeVisualizer.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/PCAPVolumeVisualizer.cpp \
        Plugins/PCAPTool/Source/PCAPTool/Private/PCAPLiveLinkMarkerSource.h \
        Plugins/PCAPTool/Source/PCAPTool/Private/PCAPLiveLinkMarkerSource.cpp
git commit -m "feat(pcap): Volume Visualizer actor — Live Link subjects as marker dots + labels, to scale in the stage FBX"
```

---

## Task 6: Final pass — colored-dot material + on-rig calibration notes

**Files:** none (content + docs); optional README note.

- [ ] **Step 1: Create the dot material (in-editor, Madi)** — in `/Game/PCAPTool/PipelineTools/VolumeViz/`, make material `M_VizDot`: a `VectorParameter` named exactly `Color` → wired to **Emissive Color**, **Shading Model = Unlit**. Assign it to the actor's `DotMaterialBase` (or set it as the default in a BP subclass).

- [ ] **Step 2: Verify** — dots now render in each subject's color; different subjects are visibly different colors; same subject keeps its color across restarts.

- [ ] **Step 3: Calibrate + save** — once the FBX is in, tune `UnitScale` / `OriginOffset` / `Yaw` until the dots sit correctly in the stage, then click **Save Alignment To Stage**. Reopen → alignment persists.

- [ ] **Step 4: Commit** (if a BP subclass or README was added)

```bash
git add Plugins/PCAPTool/Content/... Plugins/PCAPTool/Docs/...
git commit -m "feat(pcap): Volume Viz dot material + calibration notes"
```

---

## Self-review (against the spec)

- **Spec coverage:** placeable Blueprint actor ✓ (T4); stage-config drives mesh+alignment ✓ (T4 RefreshFromStageConfig); dots ✓ (T5); name labels ✓ (T5); per-subject color ✓ (T1+T4+T6); Live Link stand-in source ✓ (T3); alignment knobs + save-back ✓ (T4); editor tick ✓ (T4); StageConfig fields ✓ (T2); pure-function tests ✓ (T1). Unlabeled layer is wired (T5) but stays empty until Phase 2 — expected.
- **Placeholders:** none — every step has complete code or a concrete in-editor action.
- **Type consistency:** `FVizFrame`/`FVizMarkerGroup`/`IMarkerSource`/`PCAPViz::SubjectColor`/`ViconMMToUE` defined in T1 and used unchanged in T3-T5; `RefreshFromStageConfig`/`SaveAlignmentToStage` names consistent between header (T4) and impl (T4); `StageConfig` property name consistent.
- **Coloring caveat:** the spec assumed an MID with a `Color` param; the plan makes that material an assignable, optional `DotMaterialBase` (T4/T6) so Phase 1 builds with zero hand-made content (uncolored dots + labels still work). Refinement, not a contradiction.

---

## Phase 2 (future plan — blocked on Madi vendoring the Vicon DataStream SDK)

Outline only; written as its own plan once the SDK files are in:
1. Vendor SDK to `Source/ThirdParty/ViconDataStreamSDK/`; `Build.cs` conditional include/link/DLL-stage + `WITH_VICON_SDK=1`.
2. `FViconSDKMarkerSource : IMarkerSource` — `Connect(host)`, `EnableMarkerData`/`EnableUnlabeledMarkerData`, `GetFrame`; fill `FVizFrame.Unlabeled` (via `GetUnlabeledMarkerGlobalTranslation` + `PCAPViz::ViconMMToUE`) and per-subject `Labeled` (via `GetMarkerGlobalTranslation`).
3. Actor: when `StageConfig->bAutoConnectDataStream`, prefer the SDK source and connect to `DataStreamHost`; fall back to Live Link otherwise. Wire `Connect`/`Disconnect` CallInEditor buttons.
4. On-rig: verify axis/handedness (adjust `ViconMMToUE` + add a flip option), confirm the second DataStream connection is stable alongside Live Link.
```
