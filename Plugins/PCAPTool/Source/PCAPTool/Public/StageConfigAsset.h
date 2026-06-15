#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // enums + FRetargetConfig
#include "StageConfigAsset.generated.h"

// Stage hardware configuration. One DataAsset per stage setup
// (saved to Content/Mocap/_Roster/StageConfigs/[configName].uasset).
// Replaces the former FStageConfig struct so configs are first-class,
// shareable assets referenced by soft-ptr from FProduction / FShootDay.
UCLASS(BlueprintType)
class PCAPTOOL_API UStageConfigAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString ConfigName;          // "Home_Xsens"

    // Visual reference — a mesh laying out the physical mocap stage (any mesh: skeletal or static).
    // Shown as the Stage's card thumbnail, so you can see the stage + (with the systems below) what
    // tools it uses at a glance.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    TSoftObjectPtr<UObject> StageReferenceMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EBodySystem BodySystem = EBodySystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EFaceSystem FaceSystem = EFaceSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EAudioSystem AudioSystem = EAudioSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EVCamSystem VCamSystem = EVCamSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString LiveLinkPresetPath;  // path to the .llp preset for this config

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    ETimecodeSource TimecodeSource = ETimecodeSource::Software;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FRetargetConfig RetargetChain;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString Notes;

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
};
