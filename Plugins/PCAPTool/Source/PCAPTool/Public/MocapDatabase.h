#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"
#include "StageConfigAsset.h"
#include "MocapDatabase.generated.h"

// One day an actor is called on, for the actor's own "call sheet" (Actor Database).
struct FPCAPActorDayRef
{
    FString   ProjectCode;
    FString   ProductionName;
    FString   DayID;
    FDateTime CalendarDate = FDateTime(0);
    FString   StageName;     // the day's stage (or the production's), empty if none
};

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

    // Every day (across all productions) that calls this actor, sorted by date then day id.
    TArray<FPCAPActorDayRef> GetDaysForActor(const FString& ActorID) const;

    // Capture Setup rows on the active day (one per called performer).
    FPCAPPerformerCaptureSetup* GetCaptureSetup(const FString& ActorID);
    // Returns the existing row, or adds Defaults (ActorID forced) and returns that. Null without an active day.
    FPCAPPerformerCaptureSetup* FindOrAddCaptureSetup(const FString& ActorID, const FPCAPPerformerCaptureSetup& Defaults);

    // Day-level "HMCs used today?" flag (head-mounted facial capture), on the active day.
    bool IsHMCDay() const;
    void SetHMCDay(bool bUsed);

    // Is the active day ready to shoot? OutIssues lists what's missing (empty = ready).
    bool GetActiveDayReadiness(TArray<FString>& OutIssues) const;

    // Take-id / asset-path helpers. Slot-only 3-digit ShotID (see Phase 1 spec §4.6).
    FString BuildNextTakeID() const;
    FString BuildTakeAssetPath(const FString& TakeID, const FString& ActorID, const FString& StreamSuffix) const;
};
