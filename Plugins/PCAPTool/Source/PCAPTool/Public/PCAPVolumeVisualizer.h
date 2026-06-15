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
