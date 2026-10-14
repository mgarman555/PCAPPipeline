#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // FRetargetConfig
#include "PCAPRigSet.generated.h"

// One character a take can be reviewed on (HOME_REBUILD_HANDOFF §5.1).
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPRigSetEntry
{
    GENERATED_BODY()

    // Skeletal mesh or Blueprint to play the performance on.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UObject> Target;

    // MetaHumans share one face rig: the exported face AnimSequence plays directly.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsMetaHuman = false;

    // Non-MetaHuman only: Control Rig / Pose Asset that reads MetaHuman face curves and
    // drives this rig. Empty = the target shows "needs face mapping".
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UObject> FaceMapping;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FRetargetConfig BodyRetarget;
};

// Per-production list of review characters (Phase 5b). Lives under PCAPPaths::RigSetsDir().
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPRigSet : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig Set")
    TArray<FPCAPRigSetEntry> Targets;
};
