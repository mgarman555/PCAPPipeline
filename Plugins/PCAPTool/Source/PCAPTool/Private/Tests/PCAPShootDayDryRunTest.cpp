#include "Misc/AutomationTest.h"
#include "MocapDatabase.h"
#include "PCAPToolStatics.h"
#include "PCAPTakeOffload.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PCAPDryRunTest
{
    // Stand-in device file: deterministic bytes so the hash is stable across runs.
    static bool WriteFakeFile(const FString& Path, int32 Bytes, uint8 Seed)
    {
        TArray<uint8> Data;
        Data.SetNumUninitialized(Bytes);
        for (int32 i = 0; i < Bytes; ++i) Data[i] = static_cast<uint8>((i * 31 + Seed) & 0xFF);
        return FFileHelper::SaveArrayToFile(Data, *Path);
    }

    static const FShot* FindShot(const FShootDay& Day, const TCHAR* ShotID)
    {
        if (Day.Sessions.Num() == 0) return nullptr;
        return Day.Sessions[0].Shots.FindByPredicate([ShotID](const FShot& S) { return S.ShotID == ShotID; });
    }
}

// The weekend shoot end to end, with no hardware: set up the day, call one stereo performer,
// seed the calibration and neutral-face takes, finish the scan, record a simulated take that
// a red alarm flags, then offload it to a temp folder and check Capture Manager could import it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPShootDayDryRunTest,
    "PCAP.DryRun.ShootDay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPShootDayDryRunTest::RunTest(const FString&)
{
    const FString Actor = TEXT("performer1");

    // ── 1. Create the day ──
    UMocapDatabase* DB = NewObject<UMocapDatabase>();
    FProduction P; P.ProjectCode = TEXT("testing"); P.ProductionName = TEXT("Testing");
    FShootDay NewDay; NewDay.DayID = TEXT("001"); NewDay.CalendarDate = FDateTime(2026, 10, 17);
    P.Days = { NewDay };
    DB->Productions = { P };
    DB->ActiveProductionCode = TEXT("testing");
    DB->ActiveDayID = TEXT("001");
    FShootDay* Day = DB->GetActiveDay();
    if (!TestNotNull(TEXT("active day"), Day)) return false;

    // ── 2. Call the performer: Markerless body, OneHMC stereo, lav audio ──
    DB->SetActorCalled(Actor, true);
    TestTrue(TEXT("performer called"), DB->IsActorCalled(Actor));
    FPCAPPerformerCaptureSetup Defaults;
    Defaults.MocapSystem = TEXT("Markerless UE");
    Defaults.FacialSystem = TEXT("OneHMC");
    Defaults.bStereo = true;
    Defaults.AudioSystem = TEXT("Lavs");
    TestNotNull(TEXT("capture setup row"), DB->FindOrAddCaptureSetup(Actor, Defaults));
    TestEqual(TEXT("on the performer's call sheet"), DB->GetDaysForActor(Actor).Num(), 1);

    // ── 3. Calibration + neutral-face takes ──
    const FPCAPCalibrationNeeds Needs = UPCAPToolStatics::ResolveCalibrationNeeds(*Day, nullptr);
    TestTrue(TEXT("stereo needs an end-of-session calibration"), Needs.bStereo);
    TestEqual(TEXT("5 calibration shots"), UPCAPToolStatics::AddCalibrationShots(*Day, Needs), 5);
    for (const TCHAR* Slot : { TEXT("911"), TEXT("921"), TEXT("922"), TEXT("931"), TEXT("941") })
        TestNotNull(FString::Printf(TEXT("shot %s seeded"), Slot), PCAPDryRunTest::FindShot(*Day, Slot));
    const FShot* Neutral = PCAPDryRunTest::FindShot(*Day, TEXT("941"));
    TestTrue(TEXT("neutral face is the performer's"), Neutral && Neutral->Subjects.ContainsByPredicate(
        [&Actor](const FShotSubject& S) { return S.ActorID == Actor; }));

    // ── 4. Scan (identity poses, before the shoot) ──
    auto ScanBlocks = [DB]()
    {
        TArray<FString> Issues; DB->GetActiveDayReadiness(Issues);
        return Issues.ContainsByPredicate([](const FString& I) { return I.Contains(TEXT("identity scan")); });
    };
    TestTrue(TEXT("no scan yet: day not ready"), ScanBlocks());
    FPCAPActorScan Scan; Scan.Set = EPCAPIdentitySet::MH12; Scan.FacialSystem = TEXT("OneHMC"); Scan.bStereo = true;
    Scan.CapturedExpressions = UPCAPToolStatics::GetIdentityExpressions(EPCAPIdentitySet::MH12);
    DB->FindOrAddScan(Actor, Scan);
    TestFalse(TEXT("scan finished: no longer blocks"), ScanBlocks());

    // ── 5. Record a simulated take on a production shot ──
    FSession& Sess = Day->Sessions[0];
    FShot Shot; Shot.ShotID = TEXT("001"); Shot.Description = TEXT("Dry run");
    FShotSubject Sub; Sub.ActorID = Actor; Shot.Subjects = { Sub };
    Sess.Shots.Add(Shot);
    DB->ActiveSessionID = Sess.SessionID;
    DB->ActiveShotID = TEXT("001");
    const FString TakeID = DB->BuildNextTakeID();
    TestEqual(TEXT("take id"), TakeID, FString(TEXT("001001_001")));

    FTake Take;
    Take.TakeID = TakeID; Take.DayID = TEXT("001"); Take.ShotID = TEXT("001");
    Take.TakeNumber = TEXT("001"); Take.SessionID = Sess.SessionID;
    Take.RecordedAt = FDateTime(2026, 10, 17, 14, 0, 0);
    Take.DurationSeconds = 42.f;
    Take.DeviceStartOffsets.Add(TEXT("OneHMC"), 0.120);
    Take.DeviceStartOffsets.Add(TEXT("BodyCam"), 0.045);

    // ── 6. Flag it: a red alarm mid-take ──
    FPCAPAlarmEvent Alarm;
    Alarm.TakeOffsetSec = 17.5; Alarm.Source = TEXT("OneHMC");
    Alarm.Severity = EHMCIssueSeverity::Red; Alarm.Reason = TEXT("HMC went offline");
    Take.Alarms.Add(Alarm);
    Take.bAutoFlagged = Take.Alarms.ContainsByPredicate([](const FPCAPAlarmEvent& A) { return A.Severity == EHMCIssueSeverity::Red; });
    TestTrue(TEXT("red alarm flags the take"), Take.bAutoFlagged);
    DB->GetActiveShot()->Takes.Add(Take);
    TestEqual(TEXT("next take number moves on"), UPCAPToolStatics::GenerateNextTakeNumber(*DB->GetActiveShot()), FString(TEXT("002")));

    // ── 7. Offload to a temp folder (never D:) ──
    const FString Scratch = FPaths::ConvertRelativePathToFull(
        FPaths::AutomationTransientDir() / (TEXT("PCAPDryRun_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    const FString Devices = Scratch / TEXT("devices");
    const FString Root = Scratch / TEXT("CaptureRoot");
    IFileManager& FM = IFileManager::Get();

    struct FFake { const TCHAR* Name; EPCAPOffloadKind Kind; int32 Bytes; };
    const FFake Fakes[] = {
        { TEXT("cam0.mov"), EPCAPOffloadKind::Face,  96 * 1024 },
        { TEXT("cam1.mov"), EPCAPOffloadKind::Face,  96 * 1024 },
        { TEXT("body.mp4"), EPCAPOffloadKind::Body, 128 * 1024 },
        { TEXT("lav.wav"),  EPCAPOffloadKind::Audio, 32 * 1024 },
    };
    TArray<FPCAPOffloadFile> Files;
    uint8 Seed = 1;
    for (const FFake& F : Fakes)
    {
        FPCAPOffloadFile O; O.SourcePath = Devices / F.Name; O.Kind = F.Kind; O.ActorID = Actor;
        TestTrue(FString::Printf(TEXT("fake %s written"), F.Name), PCAPDryRunTest::WriteFakeFile(O.SourcePath, F.Bytes, Seed++));
        Files.Add(O);
    }

    const FPCAPOffloadResult R = PCAPOffload::OffloadTake(Root, TEXT("testing"), TEXT("001"), TEXT("001"), Take, Files);
    for (const FString& E : R.Errors) AddError(E);
    TestTrue(TEXT("offload ok"), R.bOk);
    TestEqual(TEXT("4 files copied"), R.Files.Num(), 4);
    for (const FPCAPOffloadedFile& F : R.Files)
    {
        TestTrue(FString::Printf(TEXT("%s verified"), *FPaths::GetCleanFilename(F.DestPath)), F.bVerified);
        TestEqual(FString::Printf(TEXT("%s hash"), *FPaths::GetCleanFilename(F.DestPath)),
            LexToString(FMD5Hash::HashFile(*F.DestPath)), LexToString(FMD5Hash::HashFile(*F.SourcePath)));
        TestTrue(TEXT("nothing written under D:"), !F.DestPath.StartsWith(TEXT("D:")));
    }

    const FString DayDir = PCAPOffload::DayDir(Root, TEXT("testing"), TEXT("001"));
    const FString FaceDir = PCAPOffload::FaceTakeDir(DayDir, TakeID, Actor);
    TestTrue(TEXT("audio rides with the face take"), FM.FileExists(*(FaceDir / TEXT("lav.wav"))));
    TestTrue(TEXT("body video in body/"), FM.FileExists(*(PCAPOffload::BodyTakeDir(DayDir, TakeID) / TEXT("body.mp4"))));

    // take.json carries identity, the flag, the alarm and the start offsets.
    FString Json;
    TestTrue(TEXT("manifest written"), FFileHelper::LoadFileToString(Json, *R.ManifestPath));
    TSharedPtr<FJsonObject> M;
    if (TestTrue(TEXT("manifest parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), M) && M.IsValid()))
    {
        TestEqual(TEXT("manifest take"), M->GetStringField(TEXT("take")), TakeID);
        TestTrue(TEXT("manifest flagged"), M->GetBoolField(TEXT("autoFlagged")));
        TestEqual(TEXT("manifest alarms"), M->GetArrayField(TEXT("alarms")).Num(), 1);
        TestEqual(TEXT("manifest files"), M->GetArrayField(TEXT("files")).Num(), 4);
        TestTrue(TEXT("manifest start offsets"), M->GetObjectField(TEXT("deviceStartOffsets"))->HasField(TEXT("OneHMC")));
    }

    // ── 8. Capture Manager could import what landed ──
    const TArray<FString> FaceProblems = PCAPOffload::CheckIngestFolder(DayDir / TEXT("face"), 2);
    const TArray<FString> BodyProblems = PCAPOffload::CheckIngestFolder(DayDir / TEXT("body"), 1);
    for (const FString& E : FaceProblems) AddError(TEXT("face ingest: ") + E);
    for (const FString& E : BodyProblems) AddError(TEXT("body ingest: ") + E);
    TestEqual(TEXT("face folder importable (stereo)"), FaceProblems.Num(), 0);
    TestEqual(TEXT("body folder importable (mono)"), BodyProblems.Num(), 0);

    // ── 9. Re-running is safe; a different file in the way is never overwritten ──
    TestTrue(TEXT("second offload is a no-op success"),
        PCAPOffload::OffloadTake(Root, TEXT("testing"), TEXT("001"), TEXT("001"), Take, Files).bOk);
    PCAPDryRunTest::WriteFakeFile(Files[0].SourcePath, 96 * 1024, 200);   // device now has a different cam0
    const FString Cam0Dest = FaceDir / TEXT("cam0.mov");
    const FString Cam0Before = LexToString(FMD5Hash::HashFile(*Cam0Dest));
    const FPCAPOffloadResult Clash = PCAPOffload::OffloadTake(Root, TEXT("testing"), TEXT("001"), TEXT("001"), Take, Files);
    TestFalse(TEXT("clash reported"), Clash.bOk);
    TestEqual(TEXT("existing file untouched"), LexToString(FMD5Hash::HashFile(*Cam0Dest)), Cam0Before);

    // ── 10. Why the layout splits face and body: the handoff's single take folder fails ──
    const FString Old = Scratch / TEXT("handoffLayout") / TEXT("Shot_001");
    PCAPDryRunTest::WriteFakeFile(Old / TakeID / TEXT("face") / TEXT("cam0.mov"), 1024, 1);
    PCAPDryRunTest::WriteFakeFile(Old / TakeID / TEXT("face") / TEXT("cam1.mov"), 1024, 2);
    PCAPDryRunTest::WriteFakeFile(Old / TakeID / TEXT("body") / TEXT("body.mp4"), 1024, 3);
    PCAPDryRunTest::WriteFakeFile(Old / TakeID / TEXT("audio") / TEXT("lav.wav"), 1024, 4);
    TestTrue(TEXT("handoff layout is not stereo-importable"), PCAPOffload::CheckIngestFolder(Old, 2).Num() > 0);

    // Names Capture Manager would rewrite are caught before they reach disk.
    TestFalse(TEXT("space in name"), PCAPOffload::IsIngestSafeName(TEXT("take 1")));
    TestFalse(TEXT("dot in name"), PCAPOffload::IsIngestSafeName(TEXT("take.1")));
    TestTrue(TEXT("take id is safe"), PCAPOffload::IsIngestSafeName(TakeID));

    FM.DeleteDirectory(*Scratch, false, true);
    return true;
}

#endif
