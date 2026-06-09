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
};
