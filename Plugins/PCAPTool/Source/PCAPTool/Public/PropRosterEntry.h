#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // EStreamStatus
#include "PropRosterEntry.generated.h"

// Permanent per-prop record. Saved to Content/Mocap/_Roster/Props/[propID].uasset.
UCLASS(BlueprintType)
class PCAPTOOL_API UPropRosterEntry : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PropID;        // "lightsaberHiltA" — canonical, permanent

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;   // "Lightsaber Hilt A"

    // The prop's mesh / asset — shown as a thumbnail in the Prop Database to verify it's correct.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UObject> PropAsset;

    // Tracking is a per-shot decision (FPropEntry), not a roster property — kept here only
    // for back-compat; the Prop Database UI does not surface it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsTracked = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bIsTracked"))
    FName DefaultLiveLinkName;   // FName — Live Link's native key type

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> ProductionHistory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Notes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;
};
