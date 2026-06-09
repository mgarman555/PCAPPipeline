#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // FBodyStreamEntry / FFaceStreamEntry / FAudioStreamEntry
#include "ActorRosterEntry.generated.h"

// Permanent per-performer record. Saved to Content/Mocap/_Roster/Actors/[actorID].uasset.
// Created once per performer; never recreated per-production or per-session.
// FirstName/LastName are metadata only — ActorID is the displayed name everywhere.
UCLASS(BlueprintType)
class PCAPTOOL_API UActorRosterEntry : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString ActorID;   // "kevinDorman" — canonical, permanent, never changes

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString FirstName;  // metadata

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    FString LastName;   // metadata

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults")
    FBodyStreamEntry DefaultBodyStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults")
    FFaceStreamEntry DefaultFaceStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defaults")
    TArray<FAudioStreamEntry> DefaultAudioStreams;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="History")
    TArray<FString> ProductionHistory;  // ["DA", "TLOU"]

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString Notes;
};
