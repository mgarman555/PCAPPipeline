#include "MocapDatabase.h"

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
