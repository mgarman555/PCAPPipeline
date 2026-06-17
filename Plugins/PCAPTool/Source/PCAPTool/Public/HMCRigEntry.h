#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // ECaptureConfiguration
#include "HMCRigEntry.generated.h"

// One physical HMC rig in the library. Saved to
// Content/Mocap/_Roster/HMCRigs/[rigName].uasset, reusable across days/productions.
// The rig's Type IS its capture configuration (mount form-factor), so it drives the
// monitor's per-config checklist when the rig is brought into the Capture tool.
// Pipeline / actor / framing / tuning are NOT here — they stay in the Capture tool.
UCLASS(BlueprintType)
class PCAPTOOL_API UHMCRigEntry : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    FString RigName;            // "Orion" — the displayed name (brand/model lives here)

    // Mount form-factor = capture configuration: phone / mono / stereo head mount, or tripod.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    ECaptureConfiguration Type = ECaptureConfiguration::StereoHeadMount;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    FString IPAddress;

    // Optional card image for the HMC Database (falls back to a placeholder when unset).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMC")
    FString Notes;
};
