#include "PCAPTakeRecorderSubsystem.h"

#include "MocapDatabase.h"
#include "PCAPToolSettings.h"
#include "PCAPToolTypes.h"
#include "PCAPToolPaths.h"
#include "StageConfigAsset.h"
#include "PCAPVCamSubsystem.h"
#include "PCAPVCamActor.h"
#include "VCamConfig.h"
#include "VCamCurveSmoothing.h"
#include "VCamSequenceSmoother.h"
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

        if (Subj.bHasBodyStream && Subj.BodyStream.StreamStatus != EStreamStatus::Connected)
        {
            OutError = FString::Printf(TEXT("%s body stream not connected."), *Subj.ActorID);
            return false;
        }
        if (Subj.bHasFaceStream && Subj.FaceStream.StreamStatus != EStreamStatus::Connected)
        {
            OutError = FString::Printf(TEXT("%s face stream not connected."), *Subj.ActorID);
            return false;
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

    UTakeRecorder* Recorder = UTakeRecorderBlueprintLibrary::StartRecording(LevelSequence, Sources, MetaData, Params);
    if (!Recorder)
    {
        OutError = TEXT("Take Recorder failed to start — see Output Log.");
        return false;
    }
    // RecordState flips to Capturing via HandleTakeStarted.
    return true;
}

void UPCAPTakeRecorderSubsystem::StopRecord()
{
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

void UPCAPTakeRecorderSubsystem::HandleTakeStarted()
{
    SetState(EPCAPRecordState::Capturing);
}

void UPCAPTakeRecorderSubsystem::HandleTakeFinished(ULevelSequence* SequenceAsset)
{
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

    // Offline take-smoothing (opt-in, non-destructive) — the 4.26 VcamSequencer post-pass,
    // rebuilt on 5.8 double channels. Runs only when the active vcam config selects a method.
    if (PendingHasVCam && SequenceAsset && GEngine)
    {
        if (UPCAPVCamSubsystem* VCamSys = GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>())
        {
            if (UPCAPVCamConfig* Cfg = VCamSys->GetActiveConfig())
            {
                const FPCAPVCamTakeSmoothingConfig& TS = Cfg->TakeSmoothing;
                if (TS.TranslationMethod != EPCAPVCamSmoothMethod::None ||
                    TS.RotationMethod    != EPCAPVCamSmoothMethod::None)
                {
                    FVCamSmoothingSettings S;
                    S.TranslationMethod   = static_cast<EVCamSmoothMethod>((uint8)TS.TranslationMethod);
                    S.RotationMethod      = static_cast<EVCamSmoothMethod>((uint8)TS.RotationMethod);
                    S.ResamplingFps       = TS.ResamplingFps;
                    S.TranslationCutoffHz = TS.TranslationCutoffHz;
                    S.RotationCutoffHz    = TS.RotationCutoffHz;
                    S.RotationSlerpBlend  = TS.RotationSlerpBlend;
                    FVCamSequenceSmoother::SmoothRecordedVCam(SequenceAsset, VCamSys->GetOrCreateVCamActor(), S);
                }
            }
        }
    }

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
