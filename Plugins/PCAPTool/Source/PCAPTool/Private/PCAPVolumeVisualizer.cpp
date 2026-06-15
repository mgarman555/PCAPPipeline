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
