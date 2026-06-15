#include "Misc/AutomationTest.h"
#include "MocapDatabase.h"
#include "ActorRosterEntry.h"
#include "PCAPToolStatics.h"
#include "PCAPToolTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// TakeID composition — slot-only 3-digit ShotID: Day(3)+Shot(3)_Take(3).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPGenerateTakeIDTest,
    "PCAP.DataModel.GenerateTakeID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPGenerateTakeIDTest::RunTest(const FString&)
{
    TestEqual(TEXT("slot-3 composition"),
        UPCAPToolStatics::GenerateTakeID(TEXT("001"), TEXT("003"), TEXT("004")),
        FString(TEXT("001003_004")));
    return true;
}

// Asset path — actor segment present when ActorID set, omitted when empty.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBuildTakeAssetPathTest,
    "PCAP.DataModel.BuildTakeAssetPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBuildTakeAssetPathTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");
    DB->ActiveSessionID      = TEXT("S01");
    DB->ActiveShotID         = TEXT("003");

    TestEqual(TEXT("body path"),
        DB->BuildTakeAssetPath(TEXT("001003_004"), TEXT("kevinDorman"), TEXT("mocap")),
        FString(TEXT("/Game/PCAPTool/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_kevinDorman_mocap")));

    TestEqual(TEXT("vcam path (no actor segment)"),
        DB->BuildTakeAssetPath(TEXT("001003_004"), FString(), TEXT("VCam")),
        FString(TEXT("/Game/PCAPTool/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_VCam")));
    return true;
}

// Next take id derives from the active shot's existing takes (max+1).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBuildNextTakeIDTest,
    "PCAP.DataModel.BuildNextTakeID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBuildNextTakeIDTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();

    FProduction P; P.ProjectCode = TEXT("DA");
    FShootDay   D; D.DayID       = TEXT("001");
    FSession    S; S.SessionID   = TEXT("S01");
    FShot    Shot; Shot.ShotID   = TEXT("003");
    FTake T1; T1.TakeNumber = TEXT("001");
    FTake T2; T2.TakeNumber = TEXT("002");
    Shot.Takes.Add(T1);
    Shot.Takes.Add(T2);
    S.Shots.Add(Shot);
    D.Sessions.Add(S);
    P.Days.Add(D);
    DB->Productions.Add(P);

    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");
    DB->ActiveSessionID      = TEXT("S01");
    DB->ActiveShotID         = TEXT("003");

    TestEqual(TEXT("next take id after 2 takes"),
        DB->BuildNextTakeID(), FString(TEXT("001003_003")));
    return true;
}

// Roster copy — fields copied, stream gates derived, inactive by default.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPMakeShotSubjectFromRosterTest,
    "PCAP.DataModel.MakeShotSubjectFromRoster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPMakeShotSubjectFromRosterTest::RunTest(const FString&)
{
    UActorRosterEntry* E = NewObject<UActorRosterEntry>();
    E->ActorID = TEXT("kevinDorman");
    E->DefaultBodyStream.LiveLinkSubjectName = FName(TEXT("kevinDorman_mocap"));
    // DefaultFaceStream left unset → bHasFaceStream should be false.

    const FShotSubject Subject = UPCAPToolStatics::MakeShotSubjectFromRoster(E);
    TestEqual (TEXT("actor id copied"),     Subject.ActorID, FString(TEXT("kevinDorman")));
    TestTrue  (TEXT("has body stream"),     Subject.bHasBodyStream);
    TestFalse (TEXT("no face stream"),      Subject.bHasFaceStream);
    TestFalse (TEXT("inactive by default"), Subject.bIsActive);
    return true;
}

// Day call sheet — set/clear/idempotent against the active day.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPDayCalloutTest,
    "PCAP.DataModel.DayCallout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPDayCalloutTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    FProduction P; P.ProjectCode = TEXT("DA");
    FShootDay   D; D.DayID       = TEXT("001");
    P.Days.Add(D);
    DB->Productions.Add(P);
    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");

    TestFalse(TEXT("not called initially"), DB->IsActorCalled(TEXT("kevinDorman")));
    DB->SetActorCalled(TEXT("kevinDorman"), true);
    TestTrue(TEXT("called after set"), DB->IsActorCalled(TEXT("kevinDorman")));
    DB->SetActorCalled(TEXT("kevinDorman"), true);
    TestEqual(TEXT("no dupes"), DB->GetDay(TEXT("DA"), TEXT("001"))->CalledActorIDs.Num(), 1);
    DB->SetActorCalled(TEXT("kevinDorman"), false);
    TestFalse(TEXT("removed after clear"), DB->IsActorCalled(TEXT("kevinDorman")));
    return true;
}

// Active-day readiness — lists what's missing; empty issues = ready.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPDayReadinessTest,
    "PCAP.DataModel.DayReadiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPDayReadinessTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    TArray<FString> Issues;

    // Nothing set → not ready, with project/day/stage/actors all flagged.
    TestFalse(TEXT("empty db not ready"), DB->GetActiveDayReadiness(Issues));
    TestTrue(TEXT("flags missing project"), Issues.Contains(TEXT("No project selected")));

    // Project + day + a called actor, but no stage → still flags the stage only.
    FProduction P; P.ProjectCode = TEXT("DA");
    FShootDay   D; D.DayID = TEXT("001"); D.CalledActorIDs.Add(TEXT("kevinDorman"));
    P.Days.Add(D);
    DB->Productions.Add(P);
    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID = TEXT("001");

    TestFalse(TEXT("no stage → not ready"), DB->GetActiveDayReadiness(Issues));
    TestTrue(TEXT("flags missing stage"), Issues.Contains(TEXT("No stage set")));
    TestFalse(TEXT("actors not flagged"), Issues.Contains(TEXT("No actors called")));
    return true;
}

// SeedNewShootDay — seeded shots/takes use the slot-only 3-digit ShotID ("003"),
// NOT day+slot ("001003"), so BuildNextTakeID composes a valid DDDSSS_NNN take id.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPSeedNewShootDayTest,
    "PCAP.DataModel.SeedNewShootDay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPSeedNewShootDayTest::RunTest(const FString&)
{
    const FShootDay Day = UPCAPToolStatics::SeedNewShootDay(TEXT("001"), FDateTime(2026, 6, 12));
    TestEqual(TEXT("one session"), Day.Sessions.Num(), 1);
    if (Day.Sessions.Num() != 1) return false;
    const FSession& Sess = Day.Sessions[0];
    TestEqual(TEXT("six seeded shots"), Sess.Shots.Num(), 6);

    auto FindShot = [&Sess](const TCHAR* ShotID) -> const FShot*
    {
        return Sess.Shots.FindByPredicate([ShotID](const FShot& S){ return S.ShotID == ShotID; });
    };

    // Slot-only ShotIDs present; the buggy day+slot form ("001901"/"001003") absent.
    TestNotNull(TEXT("calibration slot 901"), FindShot(TEXT("901")));
    TestNotNull(TEXT("production slot 003"),  FindShot(TEXT("003")));
    TestNull   (TEXT("no day+slot 001901"),   FindShot(TEXT("001901")));
    TestNull   (TEXT("no day+slot 001003"),   FindShot(TEXT("001003")));

    // A seeded take's ShotID is slot-only and its TakeID is the canonical DDDSSS_NNN.
    if (const FShot* Cal = FindShot(TEXT("901")))
    {
        TestEqual(TEXT("calibration preseeded 1 take"), Cal->Takes.Num(), 1);
        if (Cal->Takes.Num() == 1)
        {
            TestEqual(TEXT("take ShotID slot-only"), Cal->Takes[0].ShotID, FString(TEXT("901")));
            TestEqual(TEXT("take id canonical"),     Cal->Takes[0].TakeID, FString(TEXT("001901_001")));
        }
    }

    // BuildNextTakeID against a seeded production shot (no takes) yields DDDSSS_001.
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    FProduction P; P.ProjectCode = TEXT("DA"); P.Days.Add(Day);
    DB->Productions.Add(P);
    DB->ActiveProductionCode = TEXT("DA");
    DB->ActiveDayID          = TEXT("001");
    DB->ActiveSessionID      = Sess.SessionID;
    DB->ActiveShotID         = TEXT("003");
    TestEqual(TEXT("next take id for seeded shot 003"), DB->BuildNextTakeID(), FString(TEXT("001003_001")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
