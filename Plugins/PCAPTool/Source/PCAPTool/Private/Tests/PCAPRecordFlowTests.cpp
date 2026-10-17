#include "Misc/AutomationTest.h"
#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolSubsystem.h"
#include "PCAPTakeRecorderSubsystem.h"
#include "PCAPTakeOffload.h"
#include "PCAPDeviceInterfaces.h"
#include "Devices/MarkerlessBodyCamera.h"
#include "Devices/UsbAudioInput.h"
#include "PCAPToolPaths.h"
#include "AudioCaptureCore.h"
#include "TakeRecorderMicrophoneAudioManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Sound/SoundWave.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PCAPRecordFlowTest
{
    // A day with one performer on Markerless body, and production shot 001 calling them.
    static UMocapDatabase* MakeDay(const FString& Actor, const FString& Mocap)
    {
        UMocapDatabase* DB = NewObject<UMocapDatabase>(GetTransientPackage(), NAME_None, RF_Transient);
        FProduction P; P.ProjectCode = TEXT("testing");
        FShootDay D; D.DayID = TEXT("001"); D.CalledActorIDs = { Actor };
        FPCAPPerformerCaptureSetup Row; Row.ActorID = Actor; Row.MocapSystem = Mocap;
        D.CaptureSetups = { Row };
        FSession S; S.SessionID = TEXT("S01");
        FShot Shot; Shot.ShotID = TEXT("001");
        FShotSubject Sub; Sub.ActorID = Actor; Sub.bIsActive = true;
        Shot.Subjects = { Sub };
        // The neutral-face shot as AddCalibrationShots builds it (face stream, no Live Link).
        FShot Neutral; Neutral.ShotID = TEXT("941"); Neutral.ShotType = EShotType::Calibration;
        FShotSubject NSub; NSub.ActorID = Actor; NSub.bIsActive = true; NSub.bHasFaceStream = true;
        Neutral.Subjects = { NSub };
        S.Shots = { Shot, Neutral };
        D.Sessions = { S };
        P.Days = { D };
        DB->Productions = { P };
        DB->ActiveProductionCode = TEXT("testing"); DB->ActiveDayID = TEXT("001");
        DB->ActiveSessionID = TEXT("S01"); DB->ActiveShotID = TEXT("001");
        return DB;
    }

    // Points the settings at a test database / capture root for the test, restored after.
    struct FSettingsScope
    {
        UPCAPToolSettings* S;
        TSoftObjectPtr<UMocapDatabase> OldDB;
        FString OldRoot, OldAudio;
        FAudioInputDeviceProperty OldTakeRecorderInput = GetMutableDefault<UTakeRecorderMicrophoneAudioManager>()->GetAudioInputDevice();
        FSettingsScope(UMocapDatabase* DB, const FString& Root) : S(UPCAPToolSettings::Get())
        {
            OldDB = S->DatabaseAsset; OldRoot = S->CaptureRoot.Path; OldAudio = S->AudioInputDevice;
            S->DatabaseAsset = DB; S->CaptureRoot.Path = Root;   // in memory only, never saved
        }
        void Restore()
        {
            S->DatabaseAsset = OldDB; S->CaptureRoot.Path = OldRoot; S->AudioInputDevice = OldAudio;
            GetMutableDefault<UTakeRecorderMicrophoneAudioManager>()->SetAudioInputDevice(OldTakeRecorderInput);
        }
    };

    static FString FocusriteInput()
    {
        Audio::FAudioCapture Enum;
        TArray<Audio::FCaptureDeviceInfo> Devices;
        Enum.GetCaptureDevicesAvailable(Devices);
        for (const Audio::FCaptureDeviceInfo& D : Devices) if (D.DeviceName.Contains(TEXT("Focusrite"))) return D.DeviceName;
        return FString();
    }

    static bool CameraPresent()
    {
        const UPCAPToolSettings* S = GetDefault<UPCAPToolSettings>();
        const FString FFmpeg = S->FFmpegPath.FilePath.IsEmpty() ? FString(TEXT("ffmpeg.exe")) : S->FFmpegPath.FilePath;
        int32 Rc = 0; FString Out, Err;
        FPlatformProcess::ExecProcess(*FFmpeg, TEXT("-hide_banner -list_devices true -f dshow -i dummy"), &Rc, &Out, &Err);
        return (Out + Err).Contains(FString::Printf(TEXT("\"%s\""), *S->BodyCameraDevice));
    }
}

// Readiness: Markerless performers need the body camera; device-backed face streams (no Live
// Link subject, e.g. the neutral-face shot) no longer block Record.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPRecordReadinessTest,
    "PCAP.Record.Readiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPRecordReadinessTest::RunTest(const FString&)
{
    UPCAPToolSubsystem* Tool = GEngine->GetEngineSubsystem<UPCAPToolSubsystem>();
    UPCAPTakeRecorderSubsystem* Rec = GEngine->GetEngineSubsystem<UPCAPTakeRecorderSubsystem>();
    if (!TestNotNull(TEXT("subsystems"), Tool) || !TestNotNull(TEXT("recorder"), Rec)) return false;
    Tool->DisconnectBodyCamera();

    UMocapDatabase* DB = PCAPRecordFlowTest::MakeDay(TEXT("performer1"), TEXT("Markerless UE"));
    PCAPRecordFlowTest::FSettingsScope Scope(DB, FPaths::AutomationTransientDir());
    FString Err;
    TestFalse(TEXT("markerless without body camera: blocked"), Rec->AreActiveStreamsReady(Err));
    TestTrue(TEXT("reason names the body camera"), Err.Contains(TEXT("Body Camera")));

    DB->GetActiveDay()->CaptureSetups[0].MocapSystem = TEXT("Shogun Live 1.19");
    TestTrue(TEXT("non-markerless performer with no Live Link streams: ready"), Rec->AreActiveStreamsReady(Err));

    DB->ActiveShotID = TEXT("941");
    TestTrue(TEXT("neutral-face shot (device-backed face) is not blocked"), Rec->AreActiveStreamsReady(Err));
    Scope.Restore();
    return true;
}

// Real Record through Take Recorder with the real body camera: the file lands in the take
// folder, the take is harvested with its capture folder and body start offset.
// Skips (passes with a note) when the configured camera is not plugged in.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPRecordFlowBodyCameraTest,
    "PCAP.Hardware.RecordWithBodyCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPRecordFlowBodyCameraTest::RunTest(const FString&)
{
    if (!PCAPRecordFlowTest::CameraPresent())
    {
        AddInfo(TEXT("Skipped: body camera not plugged in."));
        return true;
    }
    UPCAPToolSubsystem* Tool = GEngine->GetEngineSubsystem<UPCAPToolSubsystem>();
    UPCAPTakeRecorderSubsystem* Rec = GEngine->GetEngineSubsystem<UPCAPTakeRecorderSubsystem>();
    if (!TestNotNull(TEXT("subsystems"), Tool) || !TestNotNull(TEXT("recorder"), Rec)) return false;

    const FString Root = FPaths::ConvertRelativePathToFull(
        FPaths::AutomationTransientDir() / (TEXT("PCAPRecord_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UMocapDatabase* DB = PCAPRecordFlowTest::MakeDay(TEXT("performer1"), TEXT("Markerless UE"));
    DB->AddToRoot();
    TSharedRef<PCAPRecordFlowTest::FSettingsScope> Scope = MakeShared<PCAPRecordFlowTest::FSettingsScope>(DB, Root);
    Tool->ConnectBodyCamera();
    // Audio rides along when a known-good input exists (the Focusrite on the P620).
    const FString AudioName = PCAPRecordFlowTest::FocusriteInput();
    if (!AudioName.IsEmpty()) { Scope->S->AudioInputDevice = AudioName; Tool->ConnectAudioInput(); }

    struct FState { double T0 = 0; int32 Step = 0; };
    TSharedRef<FState> St = MakeShared<FState>();
    St->T0 = FPlatformTime::Seconds();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tool, Rec, DB, Root, Scope, St, AudioName]() -> bool
    {
        const double Now = FPlatformTime::Seconds();
        TSharedPtr<IPCAPBodySource> Body = Tool->GetBodyCamera();
        auto Finish = [&]()
        {
            Tool->DisconnectBodyCamera();
            Tool->DisconnectAudioInput();
            Rec->FinishReview();
            Scope->Restore();
            DB->RemoveFromRoot();
            IFileManager::Get().DeleteDirectory(*Root, false, true);
            return true;
        };

        switch (St->Step)
        {
        case 0:   // wait for the live preview
        {
            TSharedPtr<IPCAPAudioSource> AudioIn = Tool->GetAudioInput();
            const bool bAudioReady = AudioName.IsEmpty() || (AudioIn.IsValid() && AudioIn->GetHealth().bConnected);
            if (Body.IsValid() && Body->GetHealth().bConnected && bAudioReady)
            {
                FString Err;
                const bool bStarted = Rec->StartRecordForActiveShot(Err);
                if (!TestTrue(FString::Printf(TEXT("Record starts (%s)"), *Err), bStarted)) return Finish();
                TestTrue(TEXT("body camera recording"), Body->GetHealth().bRecording);
                St->Step = 1; St->T0 = Now;
            }
            else if (Now - St->T0 > 10.0) { AddError(TEXT("Body camera never went live.")); return Finish(); }
            return false;
        }

        case 1:   // record ~8 s
            if (Now - St->T0 < 8.0) return false;   // covers Take Recorder's countdown
            TestEqual(TEXT("Take Recorder capturing"), Rec->GetRecordState(), EPCAPRecordState::Capturing);
            Rec->StopRecord();
            St->Step = 2; St->T0 = Now;
            return false;

        case 2:   // wait for the harvest and the body file to close
            if (Rec->GetRecordState() == EPCAPRecordState::Reviewing && Body.IsValid() && !Body->GetHealth().bRecording)
            {
                const FShot* Shot = DB->GetActiveShot();
                if (TestTrue(TEXT("take harvested"), Shot && Shot->Takes.Num() == 1))
                {
                    const FTake& Take = Shot->Takes[0];
                    TestEqual(TEXT("take id"), Take.TakeID, FString(TEXT("001001_001")));
                    TestEqual(TEXT("capture folder"), Take.CaptureFolder, PCAPOffload::DayDir(Root, TEXT("testing"), TEXT("001")));
                    const double* Off = Take.DeviceStartOffsets.Find(TEXT("BodyCam"));
                    TestTrue(TEXT("body start offset logged"), Off != nullptr);
                    if (Off) AddInfo(FString::Printf(TEXT("Body camera first frame %.3f s after Take Recorder start"), *Off));
                    FPCAPTakeIdentity Id; Id.CaptureFolder = Take.CaptureFolder; Id.TakeID = Take.TakeID;
                    const FString File = FMarkerlessBodyCamera::RecordFilePath(Id);
                    TestTrue(TEXT("body video in the take folder"), IFileManager::Get().FileExists(*File));
                    TestTrue(TEXT("body video has content"), IFileManager::Get().FileSize(*File) > 100 * 1024);
                    TestTrue(TEXT("Take Recorder sequence"), !Take.MasterSequence.IsNull());
                    if (!AudioName.IsEmpty())
                    {
                        // Take Recorder saves the audio under <Shot>/<TakeID>_Subscenes/Audio/, beside the take folder.
                        const FString ShotPath = FString::Printf(TEXT("%s/testing/Day_001/Session_S01/Shot_001"), *PCAPPaths::Productions());
                        TArray<FAssetData> Assets;
                        IAssetRegistry::Get()->GetAssetsByPath(FName(*ShotPath), Assets, true);
                        const FString TakeID = Take.TakeID;
                        const bool bSound = Assets.ContainsByPredicate([&TakeID](const FAssetData& A)
                        { return A.AssetClassPath == USoundWave::StaticClass()->GetClassPathName() && A.AssetName.ToString().Contains(TakeID); });
                        TestTrue(FString::Printf(TEXT("audio SoundWave recorded from %s"), *AudioName), bSound);
                    }
                }
                TestTrue(TEXT("preview resumes after the take"), Body->GetHealth().LastError.IsEmpty());
                return Finish();
            }
            if (Now - St->T0 > 15.0) { AddError(TEXT("Take never finished.")); return Finish(); }
            return false;
        }
        return Finish();
    }));
    return true;
}

#endif
