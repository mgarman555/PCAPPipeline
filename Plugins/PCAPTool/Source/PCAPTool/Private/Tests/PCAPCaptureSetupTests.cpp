#include "Misc/AutomationTest.h"
#include "PCAPToolSettings.h"
#include "PCAPToolStatics.h"
#include "StageConfigAsset.h"
#include "PCAPPerformerExtension.h"
#include "MocapDatabase.h"
#include "SPCAPStageDatabasePanel.h"
#include "SPCAPCallSheetPanel.h"
#include "SPCAPActorDatabasePanel.h"

#if WITH_DEV_AUTOMATION_TESTS

// Default Capture Systems lists (Madi, 2026-10-15) and how they flatten into dropdown entries.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCaptureSystemListsTest,
    "PCAP.CaptureSetup.SystemLists",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCaptureSystemListsTest::RunTest(const FString&)
{
    const TArray<FString> Mocap = GetDefault<UPCAPToolSettings>()->GetMocapOptions();
    // The CDO may carry DefaultGame.ini edits; test the flatten rules on a known list instead.
    const TArray<FPCAPSystemOption> Known = {
        FPCAPSystemOption(TEXT("Markerless UE")), FPCAPSystemOption(TEXT("Shogun Live"), { TEXT("1.13"), TEXT("1.19") }),
        FPCAPSystemOption(TEXT("  ")), FPCAPSystemOption(TEXT("Giant"), { TEXT("") }) };
    const TArray<FString> Flat = UPCAPToolSettings::FlattenOptions(Known);
    TestEqual(TEXT("flat count"), Flat.Num(), 4);
    TestTrue(TEXT("versioned 1.13"), Flat.Contains(TEXT("Shogun Live 1.13")));
    TestTrue(TEXT("versioned 1.19"), Flat.Contains(TEXT("Shogun Live 1.19")));
    TestFalse(TEXT("bare versioned name not listed"), Flat.Contains(TEXT("Shogun Live")));
    TestTrue(TEXT("empty version = bare name"), Flat.Contains(TEXT("Giant")));
    TestTrue(TEXT("CDO mocap list non-empty"), Mocap.Num() > 0);

    const TArray<FPCAPSystemOption> Audio = {
        FPCAPSystemOption(TEXT("Internal")), FPCAPSystemOption(TEXT("Lavs")), FPCAPSystemOption(TEXT("iPhone"), {}, TEXT("Rokoko iPhone")) };
    const TArray<FString> WithHMC = UPCAPToolSettings::AudioOptionsFor(Audio, TEXT("OneHMC"));
    TestEqual(TEXT("HMC audio count"), WithHMC.Num(), 2);
    TestFalse(TEXT("no iPhone audio with OneHMC"), WithHMC.Contains(TEXT("iPhone")));
    TestTrue(TEXT("iPhone audio with Rokoko iPhone"), UPCAPToolSettings::AudioOptionsFor(Audio, TEXT("rokoko iphone")).Contains(TEXT("iPhone")));
    return true;
}

// Calibration slots + seeding: audio / facial start / body always, facial end only for stereo.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCalibrationShotsTest,
    "PCAP.CaptureSetup.CalibrationShots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCalibrationShotsTest::RunTest(const FString&)
{
    for (const TCHAR* S : { TEXT("901"), TEXT("911"), TEXT("921"), TEXT("922"), TEXT("931") })
        TestTrue(FString::Printf(TEXT("%s is calibration"), S), UPCAPToolStatics::IsCalibrationSlot(S));
    for (const TCHAR* S : { TEXT("902"), TEXT("903"), TEXT("001"), TEXT("941"), TEXT("91"), TEXT("9a1") })
        TestFalse(FString::Printf(TEXT("%s is not calibration"), S), UPCAPToolStatics::IsCalibrationSlot(S));

    auto Has = [](const FShootDay& D, const TCHAR* Slot)
    { return D.Sessions.Num() > 0 && D.Sessions[0].Shots.ContainsByPredicate([Slot](const FShot& S){ return S.ShotID == Slot; }); };

    FShootDay Day; Day.DayID = TEXT("001");
    const FPCAPCalibrationNeeds Unknown = UPCAPToolStatics::ResolveCalibrationNeeds(Day, nullptr);
    TestEqual(TEXT("unknown day: 3 shots"), UPCAPToolStatics::AddCalibrationShots(Day, Unknown), 3);
    TestEqual(TEXT("session created"), Day.Sessions.Num(), 1);
    TestTrue(TEXT("911"), Has(Day, TEXT("911")));
    TestTrue(TEXT("921"), Has(Day, TEXT("921")));
    TestTrue(TEXT("931"), Has(Day, TEXT("931")));
    TestFalse(TEXT("no 922 without stereo"), Has(Day, TEXT("922")));
    TestEqual(TEXT("idempotent"), UPCAPToolStatics::AddCalibrationShots(Day, Unknown), 0);
    if (Day.Sessions.Num() > 0 && Day.Sessions[0].Shots.Num() > 0)
        TestEqual(TEXT("typed calibration"), Day.Sessions[0].Shots[0].ShotType, EShotType::Calibration);

    // A called performer on stereo facial adds the end-of-session board take.
    Day.CalledActorIDs = { TEXT("friend") };
    FPCAPPerformerCaptureSetup Row; Row.ActorID = TEXT("friend"); Row.FacialSystem = TEXT("OneHMC"); Row.bStereo = true;
    Day.CaptureSetups.Add(Row);
    const FPCAPCalibrationNeeds FromRow = UPCAPToolStatics::ResolveCalibrationNeeds(Day, nullptr);
    TestTrue(TEXT("row: stereo"), FromRow.bStereo);
    TestFalse(TEXT("row: no mocap -> no body"), FromRow.bBody);
    TestFalse(TEXT("row: no audio"), FromRow.bAudio);
    TestEqual(TEXT("adds only 922"), UPCAPToolStatics::AddCalibrationShots(Day, FromRow), 1);
    TestTrue(TEXT("922 added"), Has(Day, TEXT("922")));

    // Rows for actors no longer called fall back to the stage.
    Day.CalledActorIDs.Reset();
    UStageConfigAsset* Stage = NewObject<UStageConfigAsset>();
    Stage->FacialSystem = TEXT("OneHMC"); Stage->bFacialMono = true; Stage->bFacialStereo = true;   // both = undecided
    Stage->MocapSystem = TEXT("Markerless UE");
    const FPCAPCalibrationNeeds FromStage = UPCAPToolStatics::ResolveCalibrationNeeds(Day, Stage);
    TestTrue(TEXT("stage: body"), FromStage.bBody);
    TestFalse(TEXT("stage: both modes = no end take yet"), FromStage.bStereo);
    TestFalse(TEXT("stage: no audio"), FromStage.bAudio);
    return true;
}

// Row defaults: performer's saved default beats the stage; stage mono-only gives mono.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCaptureSetupDefaultsTest,
    "PCAP.CaptureSetup.RowDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCaptureSetupDefaultsTest::RunTest(const FString&)
{
    UStageConfigAsset* Stage = NewObject<UStageConfigAsset>();
    Stage->MocapSystem = TEXT("Markerless UE");
    Stage->FacialSystem = TEXT("OneHMC");
    Stage->bFacialMono = true; Stage->bFacialStereo = false;
    Stage->AudioSystemName = TEXT("Lavs");

    const FPCAPPerformerCaptureSetup FromStage = UPCAPToolStatics::MakeDefaultCaptureSetup(TEXT("a"), Stage, nullptr);
    TestEqual(TEXT("actor id"), FromStage.ActorID, FString(TEXT("a")));
    TestEqual(TEXT("stage mocap"), FromStage.MocapSystem, FString(TEXT("Markerless UE")));
    TestEqual(TEXT("stage audio"), FromStage.AudioSystem, FString(TEXT("Lavs")));
    TestFalse(TEXT("mono-only stage -> mono"), FromStage.bStereo);

    UPCAPPerformerExtension* Ext = NewObject<UPCAPPerformerExtension>();
    Ext->DefaultFacialSystem = TEXT("Rokoko iPhone");
    Ext->bDefaultStereo = false;
    Ext->DefaultAudioSystem = TEXT("iPhone");
    const FPCAPPerformerCaptureSetup FromPerf = UPCAPToolStatics::MakeDefaultCaptureSetup(TEXT("a"), Stage, Ext);
    TestEqual(TEXT("performer facial wins"), FromPerf.FacialSystem, FString(TEXT("Rokoko iPhone")));
    TestEqual(TEXT("performer audio wins"), FromPerf.AudioSystem, FString(TEXT("iPhone")));
    TestEqual(TEXT("stage mocap kept"), FromPerf.MocapSystem, FString(TEXT("Markerless UE")));

    const FPCAPPerformerCaptureSetup Bare = UPCAPToolStatics::MakeDefaultCaptureSetup(TEXT("b"), nullptr, nullptr);
    TestTrue(TEXT("no stage: empty facial"), Bare.FacialSystem.IsEmpty());
    TestTrue(TEXT("no stage: stereo default"), Bare.bStereo);
    return true;
}

// Old stage assets: enum values carry into the string fields; existing strings are kept.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPStageLegacyMigrationTest,
    "PCAP.CaptureSetup.StageLegacyMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPStageLegacyMigrationTest::RunTest(const FString&)
{
    UStageConfigAsset* S = NewObject<UStageConfigAsset>();
    S->BodySystem = EBodySystem::Xsens;
    S->FaceSystem = EFaceSystem::Technoprops;
    S->AudioSystem = EAudioSystem::RodeWirelessGo2;
    S->MocapSystem = TEXT("Giant");   // already set: must not be overwritten
    TestTrue(TEXT("changed"), S->MigrateLegacySystems());
    TestEqual(TEXT("mocap kept"), S->MocapSystem, FString(TEXT("Giant")));
    TestEqual(TEXT("facial from enum"), S->FacialSystem, FString(TEXT("Technoprops HMC")));
    TestEqual(TEXT("audio from enum"), S->AudioSystemName, FString(TEXT("Rode Wireless Go 2")));
    TestFalse(TEXT("second pass no-op"), S->MigrateLegacySystems());
    return true;
}

// The actor's own call sheet: every day across productions that calls them, date-sorted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPActorDaysTest,
    "PCAP.CaptureSetup.ActorCallSheet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPActorDaysTest::RunTest(const FString&)
{
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    FProduction DA; DA.ProjectCode = TEXT("darkAcolyte"); DA.ProductionName = TEXT("Dark Acolyte");
    FShootDay D2; D2.DayID = TEXT("002"); D2.CalendarDate = FDateTime(2026, 10, 18); D2.CalledActorIDs = { TEXT("friend") };
    FShootDay D1; D1.DayID = TEXT("001"); D1.CalendarDate = FDateTime(2026, 10, 17); D1.CalledActorIDs = { TEXT("friend"), TEXT("other") };
    DA.Days = { D2, D1 };
    FProduction FD; FD.ProjectCode = TEXT("firstDays");
    FShootDay F1; F1.DayID = TEXT("001"); F1.CalendarDate = FDateTime(2026, 11, 2); F1.CalledActorIDs = { TEXT("other") };
    FD.Days = { F1 };
    DB->Productions = { DA, FD };

    const TArray<FPCAPActorDayRef> Friend = DB->GetDaysForActor(TEXT("friend"));
    TestEqual(TEXT("friend: 2 days"), Friend.Num(), 2);
    if (Friend.Num() == 2)
    {
        TestEqual(TEXT("sorted by date"), Friend[0].DayID, FString(TEXT("001")));
        TestEqual(TEXT("production name"), Friend[0].ProductionName, FString(TEXT("Dark Acolyte")));
    }
    TestEqual(TEXT("other: 2 days across projects"), DB->GetDaysForActor(TEXT("other")).Num(), 2);
    TestEqual(TEXT("nobody"), DB->GetDaysForActor(TEXT("nobody")).Num(), 0);

    // Capture Setup rows live on the active day.
    DB->ActiveProductionCode = TEXT("darkAcolyte"); DB->ActiveDayID = TEXT("001");
    FPCAPPerformerCaptureSetup Defaults; Defaults.FacialSystem = TEXT("OneHMC");
    FPCAPPerformerCaptureSetup* Row = DB->FindOrAddCaptureSetup(TEXT("friend"), Defaults);
    TestNotNull(TEXT("row added"), Row);
    if (Row) Row->bStereo = false;
    FPCAPPerformerCaptureSetup* Again = DB->FindOrAddCaptureSetup(TEXT("friend"), Defaults);
    TestTrue(TEXT("same row, edit kept"), Again && !Again->bStereo && Again->ActorID == TEXT("friend"));
    TestEqual(TEXT("one row"), DB->GetActiveDay()->CaptureSetups.Num(), 1);
    return true;
}

// The changed panels still construct (catches Slate assertion/crash regressions headlessly).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPPanelsConstructTest,
    "PCAP.UI.PanelsConstruct",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPPanelsConstructTest::RunTest(const FString&)
{
    TSharedRef<SPCAPStageDatabasePanel> Stage = SNew(SPCAPStageDatabasePanel);
    TSharedRef<SPCAPCallSheetPanel> Sheet = SNew(SPCAPCallSheetPanel);
    TSharedRef<SPCAPActorDatabasePanel> Actors = SNew(SPCAPActorDatabasePanel);
    TestTrue(TEXT("panels constructed"), true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
