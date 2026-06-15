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

    // Use the Vicon DataStream SDK (real labeled + unlabeled markers — what the operator
    // sees). Falls back to the Live Link solved-joint stand-in when the SDK isn't built in.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    bool bUseRawMarkers = true;

    // Vicon DataStream address, used when no StageConfig host is set. e.g. "10.56.1.25:801".
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString DataStreamHost = TEXT("localhost:801");

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
    float MarkerSize = 1.f;   // approx cm diameter (Vicon markers ~0.4–1.4cm)
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

    // Drop the current feed and reconnect (e.g. after changing the host or bUseRawMarkers).
    UFUNCTION(CallInEditor, Category="Stage")
    void Reconnect();

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
    bool bSourceIsSDK = false;
    FVizFrame Frame;

    void EnsureSource();
    FString ResolveHost() const;
    void ApplyAlignment(FVector& P) const;
    UInstancedStaticMeshComponent* GetOrCreateSubjectISM(FName Subject);
    UTextRenderComponent* GetOrCreateLabel(FName Subject);
    void UpdateInstances(UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xforms);
};
