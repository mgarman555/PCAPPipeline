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
        FString(TEXT("/Game/PCAP Tool/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_kevinDorman_mocap")));

    TestEqual(TEXT("vcam path (no actor segment)"),
        DB->BuildTakeAssetPath(TEXT("001003_004"), FString(), TEXT("VCam")),
        FString(TEXT("/Game/PCAP Tool/Productions/DA/Day_001/Session_S01/Shot_003/001003_004/001003_004_VCam")));
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

#endif // WITH_DEV_AUTOMATION_TESTS
