#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"
#include "StageConfigAsset.h"
#include "MocapDatabase.generated.h"

UCLASS(BlueprintType)
class PCAPTOOL_API UMocapDatabase : public UDataAsset
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    TArray<FProduction> Productions;

    // Roster — permanent records (soft refs to the DataAssets in _Roster/).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster")
    TArray<TSoftObjectPtr<UActorRosterEntry>> ActorRoster;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster")
    TArray<TSoftObjectPtr<UPropRosterEntry>> PropRoster;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roster")
    TArray<TSoftObjectPtr<UStageConfigAsset>> StageConfigs;

    // Active session state — set at session start, cleared at session end.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active")
    FString ActiveProductionCode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active")
    FString ActiveDayID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active")
    FString ActiveSessionID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Active")
    FString ActiveShotID;

    // Raw pointer returns cannot be BlueprintCallable — C++ only.
    FProduction* GetProductionByCode(const FString& ProjectCode);
    FShootDay*   GetDay(const FString& ProjectCode, const FString& DayID);
    FSession*    GetSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID);
    FShot*       GetShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID);
    FTake*       GetTake(const FString& ProjectCode, const FString& DayID, const FString& ShotID, const FString& TakeID);
    TArray<FTake*> GetTakesByLabel(ETakeLabel Label);
    TArray<FTake*> GetUnprocessedQueuedTakes();

    // Active-selection accessors (resolve against the Active* fields above).
    FProduction* GetActiveProduction();
    FShootDay*   GetActiveDay();
    FSession*    GetActiveSession();
    FShot*       GetActiveShot();
    UStageConfigAsset* GetActiveStageConfig() const;   // day override, else production

    // Day call sheet — operate on the active day (ActiveProductionCode + ActiveDayID).
    bool IsActorCalled(const FString& ActorID) const;
    void SetActorCalled(const FString& ActorID, bool bCalled);
    bool IsPropCalled(const FString& PropID) const;
    void SetPropCalled(const FString& PropID, bool bCalled);
    bool IsVCamCalled(const FString& VCamID) const;
    void SetVCamCalled(const FString& VCamID, bool bCalled);

    // Is the active day ready to shoot? OutIssues lists what's missing (empty = ready).
    bool GetActiveDayReadiness(TArray<FString>& OutIssues) const;

    // Take-id / asset-path helpers. Slot-only 3-digit ShotID (see Phase 1 spec §4.6).
    FString BuildNextTakeID() const;
    FString BuildTakeAssetPath(const FString& TakeID, const FString& ActorID, const FString& StreamSuffix) const;
};
