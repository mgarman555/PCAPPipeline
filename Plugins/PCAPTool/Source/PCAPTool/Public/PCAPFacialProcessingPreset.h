#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPFacialProcessingPreset.generated.h"

// MetaHuman Performance face solver to use after ingest. Names follow the solver
// options on Epic's "2. Process and Export Your Animation" page (HOME_REBUILD_HANDOFF §2):
// Stereo HMC Rig Solver for stereo-HMC-class footage, Generic Rig Solver otherwise,
// Small Face Solver when face + body are solved from one video. Append only.
UENUM(BlueprintType)
enum class EPCAPFaceSolver : uint8
{
    StereoHMCRig UMETA(DisplayName = "Stereo HMC Rig Solver"),
    GenericRig   UMETA(DisplayName = "Generic Rig Solver"),
    SmallFace    UMETA(DisplayName = "Small Face Solver")
};

// Named processing setup a Capture Setup row picks for a performer's facial stream:
// the MetaHuman Performance settings applied after ingest. Lives under
// PCAPPaths::FacialPresetsDir(). Shipped presets: MHA_StereoHMC_Default, MHA_MonoHMC_Default.
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPFacialProcessingPreset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve")
    EPCAPFaceSolver Solver = EPCAPFaceSolver::StereoHMCRig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve")
    bool bHeadStabilization = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve")
    bool bSolveTongue = false;

    // Device Class the ingested footage is expected to report (the Face Solver default
    // depends on it). Free text until Phase 5 confirms the 5.8 Capture Data field it maps to.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solve")
    FString ExpectedDeviceClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString Notes;
};
