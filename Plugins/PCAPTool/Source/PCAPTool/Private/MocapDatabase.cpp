#include "MocapDatabase.h"
#include "PCAPToolStatics.h"
#include "PCAPToolPaths.h"

namespace
{
    template<typename Pred>
    void ForEachTakeInDay(FShootDay& Day, Pred&& P)
    {
        for (FSession& Session : Day.Sessions)
            for (FShot& Shot : Session.Shots)
                for (FTake& Take : Shot.Takes)
                    P(Take);
    }
}

FProduction* UMocapDatabase::GetProductionByCode(const FString& ProjectCode)
{
    for (FProduction& Prod : Productions)
        if (Prod.ProjectCode == ProjectCode)
            return &Prod;
    return nullptr;
}

FShootDay* UMocapDatabase::GetDay(const FString& ProjectCode, const FString& DayID)
{
    FProduction* Prod = GetProductionByCode(ProjectCode);
    if (!Prod) return nullptr;
    for (FShootDay& Day : Prod->Days)
        if (Day.DayID == DayID)
            return &Day;
    return nullptr;
}

FSession* UMocapDatabase::GetSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID)
{
    FShootDay* Day = GetDay(ProjectCode, DayID);
    if (!Day) return nullptr;
    for (FSession& Session : Day->Sessions)
        if (Session.SessionID == SessionID)
            return &Session;
    return nullptr;
}

FShot* UMocapDatabase::GetShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID)
{
    FSession* Session = GetSession(ProjectCode, DayID, SessionID);
    if (!Session) return nullptr;
    for (FShot& Shot : Session->Shots)
        if (Shot.ShotID == ShotID)
            return &Shot;
    return nullptr;
}

FTake* UMocapDatabase::GetTake(const FString& ProjectCode, const FString& DayID, const FString& ShotID, const FString& TakeID)
{
    FProduction* Prod = GetProductionByCode(ProjectCode);
    if (!Prod) return nullptr;
    for (FShootDay& Day : Prod->Days)
    {
        if (Day.DayID != DayID) continue;
        for (FSession& Session : Day.Sessions)
            for (FShot& Shot : Session.Shots)
            {
                if (Shot.ShotID != ShotID) continue;
                for (FTake& Take : Shot.Takes)
                    if (Take.TakeID == TakeID)
                        return &Take;
            }
    }
    return nullptr;
}

TArray<FTake*> UMocapDatabase::GetTakesByLabel(ETakeLabel Label)
{
    TArray<FTake*> Result;
    for (FProduction& Prod : Productions)
        for (FShootDay& Day : Prod.Days)
            ForEachTakeInDay(Day, [&](FTake& Take)
            {
                if (Take.Label == Label) Result.Add(&Take);
            });
    return Result;
}

TArray<FTake*> UMocapDatabase::GetUnprocessedQueuedTakes()
{
    TArray<FTake*> Result;
    for (FProduction& Prod : Productions)
        for (FShootDay& Day : Prod.Days)
            ForEachTakeInDay(Day, [&](FTake& Take)
            {
                const bool bQueueable = (Take.Label == ETakeLabel::Best || Take.Label == ETakeLabel::Alt);
                const bool bPending   = (Take.ProcessingState.OverallStatus == EProcessingStatus::Pending);
                if (bQueueable && bPending) Result.Add(&Take);
            });
    return Result;
}

// ─── Active-selection accessors ─────────────────────────────────────────────

FProduction* UMocapDatabase::GetActiveProduction()
{
    return GetProductionByCode(ActiveProductionCode);
}

FShootDay* UMocapDatabase::GetActiveDay()
{
    return GetDay(ActiveProductionCode, ActiveDayID);
}

FSession* UMocapDatabase::GetActiveSession()
{
    return GetSession(ActiveProductionCode, ActiveDayID, ActiveSessionID);
}

FShot* UMocapDatabase::GetActiveShot()
{
    return GetShot(ActiveProductionCode, ActiveDayID, ActiveSessionID, ActiveShotID);
}

UStageConfigAsset* UMocapDatabase::GetActiveStageConfig() const
{
    UMocapDatabase* Self = const_cast<UMocapDatabase*>(this);
    if (FShootDay* Day = Self->GetActiveDay())
    {
        if (!Day->ActiveStageConfig.IsNull())
        {
            return Day->ActiveStageConfig.LoadSynchronous();
        }
    }
    if (FProduction* Prod = Self->GetActiveProduction())
    {
        if (!Prod->ActiveStageConfig.IsNull())
        {
            return Prod->ActiveStageConfig.LoadSynchronous();
        }
    }
    return nullptr;
}

// ─── Take-id / asset-path helpers ───────────────────────────────────────────

FString UMocapDatabase::BuildNextTakeID() const
{
    const FShot* Shot = const_cast<UMocapDatabase*>(this)->GetActiveShot();
    if (!Shot)
    {
        return FString();
    }
    const FString TakeNumber = UPCAPToolStatics::GenerateNextTakeNumber(*Shot);
    return UPCAPToolStatics::GenerateTakeID(ActiveDayID, ActiveShotID, TakeNumber);
}

FString UMocapDatabase::BuildTakeAssetPath(const FString& TakeID, const FString& ActorID, const FString& StreamSuffix) const
{
    const FString Base = FString::Printf(
        TEXT("%s/%s/Day_%s/Session_%s/Shot_%s/%s/"),
        *PCAPPaths::Productions(), *ActiveProductionCode, *ActiveDayID, *ActiveSessionID, *ActiveShotID, *TakeID);

    const FString AssetName = ActorID.IsEmpty()
        ? FString::Printf(TEXT("%s_%s"), *TakeID, *StreamSuffix)
        : FString::Printf(TEXT("%s_%s_%s"), *TakeID, *ActorID, *StreamSuffix);

    return Base + AssetName;
}

// ─── Day call sheet ─────────────────────────────────────────────────────────

bool UMocapDatabase::IsActorCalled(const FString& ActorID) const
{
    const FShootDay* Day = const_cast<UMocapDatabase*>(this)->GetDay(ActiveProductionCode, ActiveDayID);
    return Day && Day->CalledActorIDs.Contains(ActorID);
}

void UMocapDatabase::SetActorCalled(const FString& ActorID, bool bCalled)
{
    if (FShootDay* Day = GetDay(ActiveProductionCode, ActiveDayID))
    {
        if (bCalled) Day->CalledActorIDs.AddUnique(ActorID);
        else         Day->CalledActorIDs.Remove(ActorID);
    }
}

bool UMocapDatabase::IsPropCalled(const FString& PropID) const
{
    const FShootDay* Day = const_cast<UMocapDatabase*>(this)->GetDay(ActiveProductionCode, ActiveDayID);
    return Day && Day->CalledPropIDs.Contains(PropID);
}

void UMocapDatabase::SetPropCalled(const FString& PropID, bool bCalled)
{
    if (FShootDay* Day = GetDay(ActiveProductionCode, ActiveDayID))
    {
        if (bCalled) Day->CalledPropIDs.AddUnique(PropID);
        else         Day->CalledPropIDs.Remove(PropID);
    }
}

bool UMocapDatabase::IsVCamCalled(const FString& VCamID) const
{
    const FShootDay* Day = const_cast<UMocapDatabase*>(this)->GetDay(ActiveProductionCode, ActiveDayID);
    return Day && Day->CalledVCamIDs.Contains(VCamID);
}

void UMocapDatabase::SetVCamCalled(const FString& VCamID, bool bCalled)
{
    if (FShootDay* Day = GetDay(ActiveProductionCode, ActiveDayID))
    {
        if (bCalled) Day->CalledVCamIDs.AddUnique(VCamID);
        else         Day->CalledVCamIDs.Remove(VCamID);
    }
}

bool UMocapDatabase::IsHMCDay() const
{
    const FShootDay* Day = const_cast<UMocapDatabase*>(this)->GetDay(ActiveProductionCode, ActiveDayID);
    return Day && Day->bHMCsUsed;
}

void UMocapDatabase::SetHMCDay(bool bUsed)
{
    if (FShootDay* Day = GetDay(ActiveProductionCode, ActiveDayID))
        Day->bHMCsUsed = bUsed;
}

bool UMocapDatabase::GetActiveDayReadiness(TArray<FString>& OutIssues) const
{
    OutIssues.Reset();
    UMocapDatabase* Self = const_cast<UMocapDatabase*>(this);

    if (ActiveProductionCode.IsEmpty()) OutIssues.Add(TEXT("No project selected"));
    if (ActiveDayID.IsEmpty())          OutIssues.Add(TEXT("No shoot day selected"));
    if (!GetActiveStageConfig())        OutIssues.Add(TEXT("No stage set"));

    const FShootDay* Day = Self->GetDay(ActiveProductionCode, ActiveDayID);
    if (!Day || Day->CalledActorIDs.Num() == 0) OutIssues.Add(TEXT("No actors called"));

    return OutIssues.Num() == 0;
}
