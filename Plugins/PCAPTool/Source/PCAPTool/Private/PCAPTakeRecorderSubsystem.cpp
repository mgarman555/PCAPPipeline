#include "PCAPTakeRecorderSubsystem.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "PCAPToolPaths.h"
#include "StageConfigAsset.h"
#include "PCAPVCamSubsystem.h"
#include "PCAPVCamActor.h"
#include "PCAPToolSubsystem.h"
#include "PCAPTakeOffload.h"
#include "PCAPDeviceInterfaces.h"
#include "Devices/MarkerlessBodyCamera.h"
#include "UObject/LazyObjectPtr.h"

#include "Engine/Engine.h"
#include "LevelSequence.h"

#include "Recorder/TakeRecorderBlueprintLibrary.h"
#include "Recorder/TakeRecorderParameters.h"
#include "Recorder/TakeRecorderSubsystem.h"
#include "Recorder/TakeRecorder.h"
#include "TakeRecorderSource.h"
#include "TakeRecorderSources.h"
#include "TakeMetaData.h"
#include "TakePreset.h"

// ── Lifecycle ───────────────────────────────────────────────────────────────

void UPCAPTakeRecorderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Bind to Take Recorder's record events (the BP-library setters are deprecated in 5.4+).
    if (GEngine)
    {
        if (UTakeRecorderSubsystem* TR = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>())
        {
            TR->TakeRecorderStarted.AddDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeStarted);
            TR->TakeRecorderFinished.AddDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeFinished);
        }
    }
}

void UPCAPTakeRecorderSubsystem::Deinitialize()
{
    if (GEngine)
    {
        if (UTakeRecorderSubsystem* TR = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>())
        {
            TR->TakeRecorderStarted.RemoveDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeStarted);
            TR->TakeRecorderFinished.RemoveDynamic(this, &UPCAPTakeRecorderSubsystem::HandleTakeFinished);
        }
    }
    Super::Deinitialize();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

UMocapDatabase* UPCAPTakeRecorderSubsystem::GetDB() const
{
    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    return Settings ? Settings->GetDatabase() : nullptr;
}

void UPCAPTakeRecorderSubsystem::SetState(EPCAPRecordState NewState)
{
    if (RecordState == NewState) return;
    RecordState = NewState;
    OnRecordStateChanged.Broadcast(NewState);
}

bool UPCAPTakeRecorderSubsystem::IsRecording() const
{
    return UTakeRecorderBlueprintLibrary::IsRecording();
}

FString UPCAPTakeRecorderSubsystem::PeekNextTakeID() const
{
    UMocapDatabase* DB = GetDB();
    return DB ? DB->BuildNextTakeID() : FString();
}

bool UPCAPTakeRecorderSubsystem::AreActiveStreamsReady(FString& OutError) const
{
    OutError.Reset();
    UMocapDatabase* DB = GetDB();
    if (!DB) { OutError = TEXT("No database assigned (Project Settings → PCAP)."); return false; }

    FShot* Shot = DB->GetActiveShot();
    if (!Shot) { OutError = TEXT("No active shot selected."); return false; }

    bool bAnyActive = false;
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (!Subj.bIsActive) continue;
        bAnyActive = true;

        // Only Live Link streams gate on Live Link status. Device-backed streams (HMC, markerless
        // camera) have no Live Link subject and gate on their device below / in Phase 3.
        if (Subj.bHasBodyStream && !Subj.BodyStream.LiveLinkSubjectName.IsNone()
            && Subj.BodyStream.StreamStatus != EStreamStatus::Connected)
        {
            OutError = FString::Printf(TEXT("%s body stream not connected."), *Subj.ActorID);
            return false;
        }
        if (Subj.bHasFaceStream && !Subj.FaceStream.LiveLinkSubjectName.IsNone()
            && Subj.FaceStream.StreamStatus != EStreamStatus::Connected)
        {
            OutError = FString::Printf(TEXT("%s face stream not connected."), *Subj.ActorID);
            return false;
        }

        // Markerless body (Capture Setup row): the body camera must be live.
        if (const FPCAPPerformerCaptureSetup* Row = DB->GetCaptureSetup(Subj.ActorID))
        {
            if (Row->AudioSystem == TEXT("Lavs"))
            {
                UPCAPToolSubsystem* ToolA = GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
                TSharedPtr<IPCAPAudioSource> Audio = ToolA ? ToolA->GetAudioInput() : nullptr;
                if (!Audio.IsValid() || !Audio->GetHealth().bConnected)
                {
                    OutError = FString::Printf(TEXT("%s is on Lavs: connect the Audio input in the HMC monitor."), *Subj.ActorID);
                    return false;
                }
            }
            if (Row->MocapSystem.StartsWith(TEXT("Markerless")))
            {
                UPCAPToolSubsystem* Tool = GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
                TSharedPtr<IPCAPBodySource> Body = Tool ? Tool->GetBodyCamera() : nullptr;
                if (!Body.IsValid() || !Body->GetHealth().bConnected)
                {
                    OutError = FString::Printf(TEXT("%s uses Markerless body: connect the Body Camera in the HMC monitor."), *Subj.ActorID);
                    return false;
                }
            }
        }
        for (const FAudioStreamEntry& A : Subj.AudioStreams)
        {
            if (A.StreamStatus != EStreamStatus::Connected)
            {
                OutError = FString::Printf(TEXT("%s audio '%s' not connected."), *Subj.ActorID, *A.ChannelID);
                return false;
            }
        }
    }

    if (!bAnyActive) { OutError = TEXT("No actors are called (active) on this shot."); return false; }
    return true;
}

// ── Record flow ───────────────────────────────────────────────────────────────

bool UPCAPTakeRecorderSubsystem::StartRecordForActiveShot(FString& OutError)
{
    if (IsRecording()) { OutError = TEXT("A recording is already in progress."); return false; }
    if (!AreActiveStreamsReady(OutError)) return false;

    UMocapDatabase* DB = GetDB();
    FShot* Shot = DB->GetActiveShot();   // non-null: AreActiveStreamsReady validated it

    // Capture the take identity for the harvest step.
    PendingProductionCode = DB->ActiveProductionCode;
    PendingDayID          = DB->ActiveDayID;
    PendingSessionID      = DB->ActiveSessionID;
    PendingShotID         = DB->ActiveShotID;
    PendingTakeID         = DB->BuildNextTakeID();
    if (PendingTakeID.IsEmpty()) { OutError = TEXT("Could not derive next take id."); return false; }

    // Base sequence + sources + metadata — mirrors STakeRecorderPanel: a transient preset
    // hands us a level sequence, and the sources/metadata live as metadata on it.
    UTakePreset* Preset = NewObject<UTakePreset>(GetTransientPackage(), NAME_None, RF_Transient);
    ULevelSequence* LevelSequence = Preset->GetOrCreateLevelSequence();
    if (!LevelSequence) { OutError = TEXT("Failed to create base level sequence."); return false; }

    UTakeRecorderSources* Sources  = LevelSequence->FindOrAddMetaData<UTakeRecorderSources>();
    UTakeMetaData*        MetaData = LevelSequence->FindOrAddMetaData<UTakeMetaData>();
    if (!Sources || !MetaData) { OutError = TEXT("Failed to allocate take sources/metadata."); return false; }

    // Arm a Live Link source per active body/face subject (hybrid: a stage preset may add more).
    // NOTE: audio (TakeRecorderMicrophoneAudioSource) arming is the next reflection pass — see
    // header. Body/face here + VCam (below) cover the primary streams; audio follows.
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (!Subj.bIsActive) continue;
        if (Subj.bHasBodyStream && !Subj.BodyStream.LiveLinkSubjectName.IsNone())
        {
            AddLiveLinkSource(Sources, Subj.BodyStream.LiveLinkSubjectName);
        }
        if (Subj.bHasFaceStream && !Subj.FaceStream.LiveLinkSubjectName.IsNone())
        {
            AddLiveLinkSource(Sources, Subj.FaceStream.LiveLinkSubjectName);
        }
    }

    // Audio: a connected input records through Take Recorder's microphone source (one mono
    // 44.1 kHz SoundWave per take, rode.md §4.3).
    if (UPCAPToolSubsystem* ToolA = GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr)
    {
        TSharedPtr<IPCAPAudioSource> Audio = ToolA->GetAudioInput();
        if (Audio.IsValid() && Audio->GetHealth().bConnected && !Audio->ArmTakeRecorderSource(Sources))
        {
            OutError = TEXT("Audio input could not be armed: ") + Audio->GetHealth().LastError;
            return false;
        }
    }

    // Arm the VCam camera actor as a source if the active stage records a vcam.
    PendingHasVCam = false;
    if (UStageConfigAsset* Stage = DB->GetActiveStageConfig())
    {
        if (Stage->VCamSystem != EVCamSystem::None)
        {
            if (UPCAPVCamSubsystem* VCamSys = GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>())
            {
                if (AActor* Cam = VCamSys->GetOrCreateVCamActor())
                {
                    PendingHasVCam = AddActorSource(Sources, Cam);
                }
            }
        }
    }

    // Slate = ShotID, take number = trailing 3 digits of the TakeID ("001003_004" → 4).
    MetaData->SetSlate(PendingShotID, /*bEmitChanged*/ false);
    MetaData->SetTakeNumber(FCString::Atoi(*PendingTakeID.Right(3)), /*bEmitChanged*/ false);

    // Output path: /Game/Mocap/Productions/<Code>/Day_<Day>/Session_<Sess>/Shot_<Shot>/<TakeID>/
    FTakeRecorderParameters Params = UTakeRecorderBlueprintLibrary::GetDefaultParameters();
    Params.Project.RootTakeSaveDir.Path = FString::Printf(
        TEXT("%s/%s/Day_%s/Session_%s/Shot_%s"),
        *PCAPPaths::Productions(), *PendingProductionCode, *PendingDayID, *PendingSessionID, *PendingShotID);
    Params.Project.TakeSaveDir = PendingTakeID;   // the take's own folder

    // Body camera (markerless) records its own file into the capture folder. Started before
    // Take Recorder so a camera problem stops Record instead of leaving a take with no body.
    bPendingBody = false;
    bTakeHadBody = false;
    PendingTakeStartedAt = FDateTime(0);
    PendingCaptureFolder = PCAPOffload::DayDir(GetDefault<UPCAPToolSettings>()->CaptureRoot.Path,
        PendingProductionCode, PendingDayID);
    UPCAPToolSubsystem* Tool = GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
    TSharedPtr<IPCAPBodySource> Body = Tool ? Tool->GetBodyCamera() : nullptr;
    if (Body.IsValid() && Body->GetHealth().bConnected)
    {
        FPCAPTakeIdentity Id;
        Id.Production = PendingProductionCode; Id.DayID = PendingDayID;
        Id.ShotID = PendingShotID; Id.TakeID = PendingTakeID; Id.CaptureFolder = PendingCaptureFolder;
        Body->StartRecord(Id);
        if (!Body->GetHealth().bRecording)
        {
            OutError = TEXT("Body camera could not record: ") + Body->GetHealth().LastError;
            return false;
        }
        bPendingBody = true;
        bTakeHadBody = true;
    }

    UTakeRecorder* Recorder = UTakeRecorderBlueprintLibrary::StartRecording(LevelSequence, Sources, MetaData, Params);
    if (!Recorder)
    {
        StopBodyCamera();
        bTakeHadBody = false;
        OutError = TEXT("Take Recorder failed to start — see Output Log.");
        return false;
    }
    Recorder->OnRecordingStarted().AddUObject(this, &UPCAPTakeRecorderSubsystem::HandleRecorderStarted);
    // RecordState flips to Capturing via HandleTakeStarted.
    return true;
}

void UPCAPTakeRecorderSubsystem::StopRecord()
{
    StopBodyCamera();
    if (IsRecording())
    {
        UTakeRecorderBlueprintLibrary::StopRecording();
    }
    // RecordState flips via HandleTakeFinished.
}

bool UPCAPTakeRecorderSubsystem::RecordNextTake(FString& OutError)
{
    // One-tap: same setup, next take number. BuildNextTakeID already advances off the shot's
    // current take count, so a plain start records the next take.
    return StartRecordForActiveShot(OutError);
}

void UPCAPTakeRecorderSubsystem::FinishReview()
{
    SetState(EPCAPRecordState::Ready);
}

// ── Take Recorder callbacks ─────────────────────────────────────────────────

void UPCAPTakeRecorderSubsystem::HandleRecorderStarted(UTakeRecorder*)
{
    HandleTakeStarted();
}

void UPCAPTakeRecorderSubsystem::HandleTakeStarted()
{
    PendingTakeStartedAt = FDateTime::UtcNow();
    SetState(EPCAPRecordState::Capturing);
}

void UPCAPTakeRecorderSubsystem::HandleTakeFinished(ULevelSequence* SequenceAsset)
{
    // The adapter keeps the first-frame time until its next StartRecord, so reading it here is safe.
    FDateTime BodyFirstFrame(0);
    if (bTakeHadBody)
        if (UPCAPToolSubsystem* Tool = GEngine->GetEngineSubsystem<UPCAPToolSubsystem>())
            if (TSharedPtr<IPCAPBodySource> Body = Tool->GetBodyCamera())
                if (Body->VendorId() == FMarkerlessBodyCamera::StaticVendorId())
                    BodyFirstFrame = static_cast<FMarkerlessBodyCamera*>(Body.Get())->GetRecordFirstFrameTime();
    const bool bHadBody = bTakeHadBody;
    bTakeHadBody = false;
    StopBodyCamera();

    UMocapDatabase* DB = GetDB();
    if (!DB) { SetState(EPCAPRecordState::Ready); return; }

    FShot* Shot = DB->GetShot(PendingProductionCode, PendingDayID, PendingSessionID, PendingShotID);
    if (!Shot) { SetState(EPCAPRecordState::Ready); return; }

    FTake Take;
    Take.TakeID     = PendingTakeID;
    Take.DayID      = PendingDayID;
    Take.ShotID     = PendingShotID;
    Take.SessionID  = PendingSessionID;
    Take.TakeNumber = PendingTakeID.Right(3);
    Take.RecordedAt = FDateTime::UtcNow();
    Take.Label      = ETakeLabel::Captured;
    Take.MasterSequence = SequenceAsset;   // the assembled take
    Take.bHasVCam = PendingHasVCam;   // VCamAsset (the dedicated sub-asset) resolves in the
                                      // same later per-stream pass as Body/Face/Audio refs.
    Take.CaptureFolder = PendingCaptureFolder;
    // Start offset = first body frame relative to Take Recorder's start (alignment until a sync box).
    if (bHadBody && BodyFirstFrame.GetTicks() > 0 && PendingTakeStartedAt.GetTicks() > 0)
        Take.DeviceStartOffsets.Add(TEXT("BodyCam"), (BodyFirstFrame - PendingTakeStartedAt).GetTotalSeconds());

    // Subject manifest (record-time provenance, incl. DrivenTarget).
    for (const FShotSubject& Subj : Shot->Subjects)
    {
        if (!Subj.bIsActive) continue;
        FTakeSubjectSnapshot Snap;
        Snap.ActorID        = Subj.ActorID;
        Snap.CharacterName  = Subj.CharacterName;
        Snap.bHadBodyStream = Subj.bHasBodyStream;
        Snap.bHadFaceStream = Subj.bHasFaceStream;
        for (const FAudioStreamEntry& A : Subj.AudioStreams)
        {
            Snap.AudioChannels.Add(A.ChannelID);
        }
        Snap.DrivenTarget = Subj.DrivenTarget;
        Take.SubjectManifest.Add(Snap);
    }

    // Prop manifest.
    for (const FPropEntry& Prop : Shot->Props)
    {
        FTakePropSnapshot PS;
        PS.PropID      = Prop.PropID;
        PS.bWasTracked = Prop.bIsTracked;
        Take.PropManifest.Add(PS);
    }

    // NOTE: per-stream asset refs (BodyAnimAssets/FaceAnimAssets/AudioAssets) are resolved in a
    // later pass — they're bindings inside the recorded sequence; MasterSequence is the entry point.

    Shot->Takes.Add(Take);
    DB->MarkPackageDirty();   // persisted on the next SaveDatabase()/editor save

    SetState(EPCAPRecordState::Reviewing);   // operator labels the take, then FinishReview()
}

// ── Reflection: arm an engine-private Live Link source ──────────────────────

UClass* UPCAPTakeRecorderSubsystem::FindSourceClass(const TCHAR* ScriptPath)
{
    // The source classes live in Private/ engine modules — resolve by /Script path at runtime.
    return FindObject<UClass>(nullptr, ScriptPath);
}

void UPCAPTakeRecorderSubsystem::SetSourceFName(UObject* Source, const TCHAR* PropName, FName Value)
{
    if (!Source) return;
    if (FNameProperty* Prop = FindFProperty<FNameProperty>(Source->GetClass(), PropName))
    {
        Prop->SetPropertyValue_InContainer(Source, Value);
    }
}

bool UPCAPTakeRecorderSubsystem::AddLiveLinkSource(UObject* Sources, FName SubjectName) const
{
    UTakeRecorderSources* Src = Cast<UTakeRecorderSources>(Sources);
    if (!Src) return false;

    UClass* LiveLinkSourceClass = FindSourceClass(TEXT("/Script/LiveLinkSequencer.TakeRecorderLiveLinkSource"));
    if (!LiveLinkSourceClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] TakeRecorderLiveLinkSource class not found — is the LiveLink plugin loaded?"));
        return false;
    }

    UTakeRecorderSource* NewSrc = Src->AddSource(LiveLinkSourceClass);
    SetSourceFName(NewSrc, TEXT("SubjectName"), SubjectName);
    return NewSrc != nullptr;
}

// ── Reflection: arm an engine-private Actor source (VCam camera) ─────────────
// CONFIRM-AT-BUILD: class path + "Target" property name/type (TLazyObjectPtr<AActor>
// → FLazyObjectProperty) are the 5.7 risk points — log if either lookup is null.

void UPCAPTakeRecorderSubsystem::SetSourceLazyActor(UObject* Source, const TCHAR* PropName, AActor* Value)
{
    if (!Source) { return; }
    if (FLazyObjectProperty* Prop = FindFProperty<FLazyObjectProperty>(Source->GetClass(), PropName))
    {
        FLazyObjectPtr Lazy;
        Lazy = Value;
        Prop->SetPropertyValue_InContainer(Source, Lazy);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Actor source has no '%s' FLazyObjectProperty — confirm against 5.7 source."), PropName);
    }
}

bool UPCAPTakeRecorderSubsystem::AddActorSource(UObject* Sources, AActor* Target) const
{
    UTakeRecorderSources* Src = Cast<UTakeRecorderSources>(Sources);
    if (!Src || !Target) { return false; }

    UClass* ActorSourceClass = FindSourceClass(TEXT("/Script/TakeRecorderSources.TakeRecorderActorSource"));
    if (!ActorSourceClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] TakeRecorderActorSource class not found — confirm /Script path on 5.7."));
        return false;
    }

    UTakeRecorderSource* NewSrc = Src->AddSource(ActorSourceClass);
    SetSourceLazyActor(NewSrc, TEXT("Target"), Target);
    return NewSrc != nullptr;
}

void UPCAPTakeRecorderSubsystem::StopBodyCamera()
{
    if (!bPendingBody) return;
    bPendingBody = false;
    if (UPCAPToolSubsystem* Tool = GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr)
        if (TSharedPtr<IPCAPBodySource> Body = Tool->GetBodyCamera())
            Body->StopRecord();
}
