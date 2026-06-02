#include "PCAPToolStatics.h"

FString UPCAPToolStatics::GenerateTakeID(const FString& DayNumber, const FString& ShotSlot, const FString& TakeNumber)
{
    const FString Day  = FString::Printf(TEXT("%03d"), FCString::Atoi(*DayNumber));
    const FString Shot = FString::Printf(TEXT("%03d"), FCString::Atoi(*ShotSlot));
    const FString Take = FString::Printf(TEXT("%03d"), FCString::Atoi(*TakeNumber));
    return FString::Printf(TEXT("%s%s_%s"), *Day, *Shot, *Take);
}

FString UPCAPToolStatics::GenerateNextTakeNumber(const FShot& Shot)
{
    int32 Max = 0;
    for (const FTake& Take : Shot.Takes)
    {
        const int32 Num = FCString::Atoi(*Take.TakeNumber);
        if (Num > Max) Max = Num;
    }
    return FString::Printf(TEXT("%03d"), Max + 1);
}

FString UPCAPToolStatics::ShotSlotForType(EShotType ShotType, int32 ShotIndex)
{
    switch (ShotType)
    {
        case EShotType::Calibration: return TEXT("901");
        case EShotType::TestShot:    return TEXT("902");
        case EShotType::Retargeting: return TEXT("903");
        default:                     return FString::Printf(TEXT("%03d"), FMath::Max(1, ShotIndex));
    }
}

FShootDay UPCAPToolStatics::SeedNewShootDay(const FString& DayID, const FDateTime& CalendarDate)
{
    FShootDay Day;
    Day.DayID        = DayID;
    Day.CalendarDate = CalendarDate;

    const FString DayPadded = FString::Printf(TEXT("%03d"), FCString::Atoi(*DayID));

    auto MakeTake = [&](const FString& Slot, const FString& TakeNum) -> FTake
    {
        FTake T;
        T.TakeNumber = TakeNum;
        T.ShotID     = DayPadded + Slot;
        T.DayID      = DayID;
        T.TakeID     = GenerateTakeID(DayPadded, Slot, TakeNum);
        T.Label      = ETakeLabel::Captured;
        return T;
    };

    auto MakeShot = [&](const FString& Slot, EShotType Type, const FString& Desc, int32 PreseededTakes) -> FShot
    {
        FShot S;
        S.ShotID      = DayPadded + Slot;
        S.ShotType    = Type;
        S.Description = Desc;
        for (int32 i = 1; i <= PreseededTakes; ++i)
            S.Takes.Add(MakeTake(Slot, FString::Printf(TEXT("%03d"), i)));
        return S;
    };

    FSession Session;
    Session.SessionID = DayID + TEXT("_S01");
    Session.Label     = TEXT("Morning");

    Session.Shots.Add(MakeShot(TEXT("901"), EShotType::Calibration, TEXT("Calibration"),  1));
    Session.Shots.Add(MakeShot(TEXT("902"), EShotType::TestShot,    TEXT("Test Shot"),     5));
    Session.Shots.Add(MakeShot(TEXT("903"), EShotType::Retargeting, TEXT("Retargeting"),  1));
    Session.Shots.Add(MakeShot(TEXT("001"), EShotType::Production,  TEXT("Production 1"), 0));
    Session.Shots.Add(MakeShot(TEXT("002"), EShotType::Production,  TEXT("Production 2"), 0));
    Session.Shots.Add(MakeShot(TEXT("003"), EShotType::Production,  TEXT("Production 3"), 0));

    Day.Sessions.Add(Session);
    return Day;
}
